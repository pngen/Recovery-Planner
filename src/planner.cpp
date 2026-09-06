#include "recovery_planner/planner.hpp"
#include "recovery_planner/digest.hpp"
#include "recovery_planner/error.hpp"

#include <map>
#include <vector>
#include <mutex>
#include <algorithm>
#include <sstream>

namespace recovery_planner {

namespace {

std::string value_or(const std::optional<Duration>& v, const char* fallback) {
  return v ? std::to_string(v->value()) : fallback;
}
std::string value_or(const std::optional<ProgressUnits>& v, const char* fallback) {
  return v ? std::to_string(v->value()) : fallback;
}
std::string value_or(const std::optional<CostUnits>& v, const char* fallback) {
  return v ? std::to_string(v->value()) : fallback;
}

std::string build_payload(const RecoveryCandidate& c) {
  std::ostringstream oss;
  oss << to_string(c.strategy) << "|worker=" << c.target_worker.value()
      << "|boot=" << c.target_boot.value()
      << "|state=" << c.state_source.value()
      << "|state_gen=" << c.state_generation.value();
  return oss.str();
}

// Deterministic decision identity digest.
std::string decision_digest(const RecoveryRequest& req, const RecoveryPlan& plan,
                            const PlannerContext& ctx) {
  ByteWriter w;
  w.u64(req.request_id.value());
  w.u64(req.workload.value());
  w.u64(req.workload_generation.value());
  w.u64(req.execution.value());
  w.u64(req.failure.value());
  w.u64(ctx.epoch.value());
  // NOTE: plan instance id/generation are intentionally excluded so the digest
  // reflects only the canonical decision identity (request + selected candidate
  // + generation fences), independent of how many prior plans were created.
  w.u8(static_cast<std::uint8_t>(plan.strategy));
  w.u64(plan.candidate_id.value());
  w.u64(plan.candidate_generation.value());
  w.u64(plan.target_worker.value());
  w.u64(plan.target_boot.value());
  w.u64(plan.state_source.value());
  w.u64(plan.state_generation.value());
  w.u64(plan.authority.policy_generation.value());
  w.u64(plan.authority.compatibility_generation.value());
  w.u64(plan.authority.resource_generation.value());
  auto digest = sha256(w.data().data(), w.size());
  std::vector<std::uint8_t> bytes(digest.begin(), digest.end());
  return hex_encode(bytes);
}

// Explain why a plan would change, computed after the governing constraint.
WhatWouldChange compute_what_would_change(const RecoveryRequest& /*req*/,
                                          const std::vector<RankedCandidate>& ranked,
                                          const RecoveryPlan& plan) {
  WhatWouldChange out;
  auto add = [&](std::string cond, std::string conseq) {
    out.conditions.emplace_back(std::move(cond), std::move(conseq));
  };

  const RecoveryStrategy sel = plan.strategy;
  for (const auto& rc : ranked) {
    if (rc.candidate.candidate_id == plan.candidate_id) continue;
    const RecoveryStrategy alt = rc.candidate.strategy;
    switch (rc.feasibility.result) {
      case FeasibilityResult::DEFER:
        add(alt == RecoveryStrategy::SHADOW_PROMOTE
                ? "the shadow/warm candidate becomes READY"
                : std::string(to_string(alt)) + " target becomes READY",
            std::string(to_string(alt)) + " may overtake " + to_string(sel));
        break;
      case FeasibilityResult::INSUFFICIENT_EVIDENCE:
        add("missing evidence for " + std::string(to_string(alt)) + " is supplied",
            std::string(to_string(alt)) + " may become feasible and preferred");
        break;
      case FeasibilityResult::INFEASIBLE:
        if (rc.feasibility.violations.size()) {
          add(std::string(to_string(alt)) + " resolves its constraint (" +
                  std::string(to_string(rc.feasibility.violations.front().reason)) + ")",
              std::string(to_string(alt)) + " may become feasible");
        }
        break;
      case FeasibilityResult::REVALIDATION_REQUIRED:
        add(std::string(to_string(alt)) + " evidence is revalidated as current",
            std::string(to_string(alt)) + " may become feasible");
        break;
      default:
        // A feasible alternative that lost to the selected candidate.
        if (alt == RecoveryStrategy::RESTORE || alt == RecoveryStrategy::RECOMPUTE ||
            alt == RecoveryStrategy::SHADOW_PROMOTE || alt == RecoveryStrategy::MIGRATE) {
          add("a " + std::string(to_string(alt)) + " path with better economics appears",
              std::string(to_string(alt)) + " may overtake " + to_string(sel));
        }
        break;
    }
  }

  // Deadline governed the decision.
  if (plan.deadline.outcome == DeadlineOutcome::HARD_FEASIBLE && plan.deadline.slack.has_value() &&
      !plan.economics.expected_time.has_value() == false) {
    bool tight = plan.deadline.slack->value() < 0;
    if (!tight) {
      add("the recovery deadline increases",
          "a lower-risk but slower candidate may become preferred");
    }
  }
  if (plan.deadline.outcome == DeadlineOutcome::VIOLATED) {
    add("the recovery deadline is relaxed or the candidate speeds up",
        "the selected fallback plan may become deadline-feasible");
  }

  // Stable deterministic ordering.
  std::sort(out.conditions.begin(), out.conditions.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  return out;
}

}  // namespace

class RecoveryPlanner::Impl {
 public:
  std::shared_ptr<Clock> clock_;
  std::shared_ptr<PersistenceStore> store_;
  PlannerContext ctx_;
  mutable std::mutex mu_;
  std::map<std::uint64_t, RecoveryPlan> plans_;
  std::map<std::uint64_t, RecoveryRequest> requests_;
  std::map<std::uint64_t, std::tuple<RecoveryPlanId, RecoveryPlanGeneration, WorkerBootId>> dispatch_meta_;
  std::vector<std::string> history_;
  std::uint64_t next_plan_id_{1};
  std::uint64_t next_generation_{1};

  explicit Impl(std::shared_ptr<Clock> clock, std::shared_ptr<PersistenceStore> store)
      : clock_(std::move(clock)), store_(std::move(store)) {
    ctx_.epoch = CoordinatorEpoch(1);
    ctx_.planner_boot = PlannerBootId(1);
    ctx_.workload_generation = WorkloadGeneration(1);
    ctx_.policy_generation = PolicyGeneration(1);
    ctx_.compatibility_generation = CompatibilityGeneration(1);
    ctx_.resource_generation = ResourceGeneration(1);
    ctx_.topology_generation = TopologyGeneration(1);
    // Accept a pre-populated context via a constructor overload is not exposed;
    // tests mutate context through the public world-maintenance methods.
  }

  Timestamp now() const { return clock_ ? clock_->now() : Timestamp(0); }

  // ---- snapshots -----------------------------------------------------------
  std::vector<std::uint8_t> encode_snapshot() const;
  bool apply_snapshot(const std::vector<std::uint8_t>& bytes, bool preserve_epoch_boot);
  void save_locked();
  void load_locked();

  // ---- planning ------------------------------------------------------------
  std::vector<std::size_t> rank_feasible(RecoveryDecision& d, std::vector<std::size_t> idx) const;
  RecoveryPlan build_plan(const RecoveryRequest& req, RankedCandidate& rc, Timestamp now,
                          bool allow_violation);
  PlanExplanation build_explanation(const RecoveryRequest& req,
                                    const std::vector<RankedCandidate>& ranked,
                                    std::size_t selected,
                                    const std::vector<std::size_t>& eligible,
                                    Timestamp now) const;
  void record_history(const RecoveryRequest& req, const RecoveryDecision& d);
  void supersede_older(const RecoveryRequest& req, RecoveryPlanId keep, Timestamp now);
};

// ---------------------------------------------------------------------------
// Snapshot encoding / decoding
// ---------------------------------------------------------------------------
std::vector<std::uint8_t> RecoveryPlanner::Impl::encode_snapshot() const {
  ByteWriter w;
  w.u32(kPersistenceFormatVersion);
  w.u64(ctx_.epoch.value());
  w.u64(ctx_.planner_boot.value());
  w.u64(ctx_.workload_generation.value());
  w.u64(ctx_.policy_generation.value());
  w.u64(ctx_.compatibility_generation.value());
  w.u64(ctx_.resource_generation.value());
  w.u64(ctx_.topology_generation.value());

  // Worker registrations (id -> boot).
  w.u32(static_cast<std::uint32_t>(ctx_.worker_boots.size()));
  for (const auto& kv : ctx_.worker_boots) {
    w.u64(kv.first.value());
    w.u64(kv.second.value());
  }

  // Requests.
  w.u32(static_cast<std::uint32_t>(requests_.size()));
  for (const auto& kv : requests_) {
    const auto& r = kv.second;
    w.u64(r.request_id.value());
    w.u64(r.workload.value());
    w.u64(r.workload_generation.value());
    w.u64(r.execution.value());
    w.u64(r.execution_generation.value());
    w.u64(r.failure.value());
    w.cstring(r.reason.c_str());
    // optionals
    w.u8(r.recovery_deadline.has_value() ? 1 : 0);
    if (r.recovery_deadline) w.i64(r.recovery_deadline->value());
    w.u8(r.max_recovery_duration.has_value() ? 1 : 0);
    if (r.max_recovery_duration) w.i64(r.max_recovery_duration->value());
    w.u8(r.max_tolerated_progress_loss.has_value() ? 1 : 0);
    if (r.max_tolerated_progress_loss) w.u64(r.max_tolerated_progress_loss->value());
    w.u32(static_cast<std::uint32_t>(r.preferred_strategies.size()));
    for (auto s : r.preferred_strategies) w.u8(static_cast<std::uint8_t>(s));
    w.u32(static_cast<std::uint32_t>(r.forbidden_strategies.size()));
    for (auto s : r.forbidden_strategies) w.u8(static_cast<std::uint8_t>(s));
    w.u64(r.coordinator_epoch.value());
    w.u64(r.policy_generation.value());
    w.u64(r.topology_generation.value());
    w.i64(r.requested_at.value());
    w.u32(static_cast<std::uint32_t>(r.state_requirements.size()));
    for (const auto& s : r.state_requirements) w.cstring(s.c_str());
    w.u32(static_cast<std::uint32_t>(r.placement_constraints.size()));
    for (const auto& s : r.placement_constraints) w.cstring(s.c_str());
    w.u32(static_cast<std::uint32_t>(r.compatibility_requirements.size()));
    for (const auto& s : r.compatibility_requirements) w.cstring(s.c_str());
  }

  // Plans.
  w.u32(static_cast<std::uint32_t>(plans_.size()));
  for (const auto& kv : plans_) {
    const auto& p = kv.second;
    w.u64(p.plan_id.value());
    w.u64(p.generation.value());
    w.u64(p.request_id.value());
    w.u64(p.workload.value());
    w.u8(static_cast<std::uint8_t>(p.strategy));
    w.u64(p.candidate_id.value());
    w.u64(p.candidate_generation.value());
    w.u64(p.target_worker.value());
    w.u64(p.target_boot.value());
    w.u64(p.target_engine.value());
    w.u64(p.engine_incarnation.value());
    w.u64(p.state_source.value());
    w.u64(p.state_generation.value());
    w.u64(p.checkpoint_source.value());
    // authority
    w.u64(p.authority.coordinator_epoch.value());
    w.u64(p.authority.planner_boot.value());
    w.u64(p.authority.request_id.value());
    w.u64(p.authority.plan_generation.value());
    w.u64(p.authority.candidate_generation.value());
    w.u64(p.authority.target_boot.value());
    w.u64(p.authority.engine_incarnation.value());
    w.u64(p.authority.state_generation.value());
    w.u64(p.authority.compatibility_generation.value());
    w.u64(p.authority.resource_generation.value());
    w.u64(p.authority.policy_generation.value());
    w.u64(p.authority.topology_generation.value());
    w.u64(p.authority.workload_generation.value());
    w.u8(static_cast<std::uint8_t>(p.state));
    w.u8(static_cast<std::uint8_t>(p.execution_outcome));
    w.u8(p.outcome_published ? 1 : 0);
    w.i64(p.created_at.value());
    w.i64(p.updated_at.value());
    w.u64(p.last_attempt.value());
    w.cstring(p.dispatch_payload.c_str());
    w.cstring(p.decision_digest.c_str());
  }

  // History.
  w.u32(static_cast<std::uint32_t>(history_.size()));
  for (const auto& h : history_) w.cstring(h.c_str());

  return w.take();
}

bool RecoveryPlanner::Impl::apply_snapshot(const std::vector<std::uint8_t>& bytes,
                                           bool preserve_epoch_boot) {
  ByteReader r(bytes);
  std::uint32_t ver = r.u32();
  if (ver != kPersistenceFormatVersion || !r.ok()) return false;

  if (!preserve_epoch_boot) {
    ctx_.epoch = CoordinatorEpoch(r.u64());
    ctx_.planner_boot = PlannerBootId(r.u64());
  } else {
    r.u64();  // epoch (discard, keep advanced)
    r.u64();  // planner_boot (discard)
  }
  ctx_.workload_generation = WorkloadGeneration(r.u64());
  ctx_.policy_generation = PolicyGeneration(r.u64());
  ctx_.compatibility_generation = CompatibilityGeneration(r.u64());
  ctx_.resource_generation = ResourceGeneration(r.u64());
  ctx_.topology_generation = TopologyGeneration(r.u64());
  if (!r.ok()) return false;

  ctx_.worker_boots.clear();
  std::uint32_t wcount = r.u32();
  for (std::uint32_t i = 0; i < wcount; ++i) {
    WorkerId w(r.u64());
    WorkerBootId b(r.u64());
    ctx_.worker_boots[w] = b;
  }
  // alive_workers is dynamic and explicitly NOT restored.
  ctx_.alive_workers.clear();

  requests_.clear();
  std::uint32_t rcount = r.u32();
  for (std::uint32_t i = 0; i < rcount; ++i) {
    RecoveryRequest q;
    q.request_id = RecoveryRequestId(r.u64());
    q.workload = WorkloadId(r.u64());
    q.workload_generation = WorkloadGeneration(r.u64());
    q.execution = ExecutionId(r.u64());
    q.execution_generation = ExecutionGeneration(r.u64());
    q.failure = FailureId(r.u64());
    q.reason = r.cstring();
    if (r.u8()) q.recovery_deadline = Timestamp(r.i64());
    if (r.u8()) q.max_recovery_duration = Duration(r.i64());
    if (r.u8()) q.max_tolerated_progress_loss = ProgressUnits(r.u64());
    std::uint32_t n = r.u32();
    for (std::uint32_t j = 0; j < n; ++j) q.preferred_strategies.push_back(static_cast<RecoveryStrategy>(r.u8()));
    n = r.u32();
    for (std::uint32_t j = 0; j < n; ++j) q.forbidden_strategies.push_back(static_cast<RecoveryStrategy>(r.u8()));
    q.coordinator_epoch = CoordinatorEpoch(r.u64());
    q.policy_generation = PolicyGeneration(r.u64());
    q.topology_generation = TopologyGeneration(r.u64());
    q.requested_at = Timestamp(r.i64());
    std::uint32_t cnt;
    cnt = r.u32(); for (std::uint32_t j = 0; j < cnt; ++j) q.state_requirements.push_back(r.cstring());
    cnt = r.u32(); for (std::uint32_t j = 0; j < cnt; ++j) q.placement_constraints.push_back(r.cstring());
    cnt = r.u32(); for (std::uint32_t j = 0; j < cnt; ++j) q.compatibility_requirements.push_back(r.cstring());
    if (!r.ok()) return false;
    requests_[q.request_id.value()] = q;
  }

  plans_.clear();
  std::uint32_t pcount = r.u32();
  for (std::uint32_t i = 0; i < pcount; ++i) {
    RecoveryPlan p;
    p.plan_id = RecoveryPlanId(r.u64());
    p.generation = RecoveryPlanGeneration(r.u64());
    p.request_id = RecoveryRequestId(r.u64());
    p.workload = WorkloadId(r.u64());
    p.strategy = static_cast<RecoveryStrategy>(r.u8());
    p.candidate_id = CandidateId(r.u64());
    p.candidate_generation = CandidateGeneration(r.u64());
    p.target_worker = WorkerId(r.u64());
    p.target_boot = WorkerBootId(r.u64());
    p.target_engine = EngineId(r.u64());
    p.engine_incarnation = EngineIncarnationId(r.u64());
    p.state_source = StateId(r.u64());
    p.state_generation = StateGeneration(r.u64());
    p.checkpoint_source = CheckpointId(r.u64());
    p.authority.coordinator_epoch = CoordinatorEpoch(r.u64());
    p.authority.planner_boot = PlannerBootId(r.u64());
    p.authority.request_id = RecoveryRequestId(r.u64());
    p.authority.plan_generation = RecoveryPlanGeneration(r.u64());
    p.authority.candidate_generation = CandidateGeneration(r.u64());
    p.authority.target_boot = WorkerBootId(r.u64());
    p.authority.engine_incarnation = EngineIncarnationId(r.u64());
    p.authority.state_generation = StateGeneration(r.u64());
    p.authority.compatibility_generation = CompatibilityGeneration(r.u64());
    p.authority.resource_generation = ResourceGeneration(r.u64());
    p.authority.policy_generation = PolicyGeneration(r.u64());
    p.authority.topology_generation = TopologyGeneration(r.u64());
    p.authority.workload_generation = WorkloadGeneration(r.u64());
    p.state = static_cast<PlanLifecycleState>(r.u8());
    p.execution_outcome = static_cast<ExecutionOutcome>(r.u8());
    p.outcome_published = r.u8() != 0;
    p.created_at = Timestamp(r.i64());
    p.updated_at = Timestamp(r.i64());
    p.last_attempt = AttemptId(r.u64());
    p.dispatch_payload = r.cstring();
    p.decision_digest = r.cstring();
    if (!r.ok()) return false;
    plans_[p.plan_id.value()] = p;
  }

  history_.clear();
  std::uint32_t hcount = r.u32();
  for (std::uint32_t i = 0; i < hcount; ++i) history_.push_back(r.cstring());
  if (!r.ok()) return false;
  return true;
}

void RecoveryPlanner::Impl::save_locked() {
  if (!store_) return;
  std::vector<std::uint8_t> snap = encode_snapshot();
  const auto& b = snap;
  // Recompute a payload CRC is handled by the store; here we trust the store.
  (void)b;
  store_->save("planner_state", snap);
}

void RecoveryPlanner::Impl::load_locked() {
  if (!store_) return;
  auto blob = store_->load("planner_state");
  if (!blob) return;
  apply_snapshot(*blob, false);
}

// ---------------------------------------------------------------------------
// Ranking
// ---------------------------------------------------------------------------
std::vector<std::size_t> RecoveryPlanner::Impl::rank_feasible(
    RecoveryDecision& d, std::vector<std::size_t> idx) const {
  std::sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
    return candidate_precedes(d.ranked[a].candidate, d.ranked[b].candidate,
                              d.ranked[a].economics, d.ranked[b].economics,
                              d.ranked[a].deadline, d.ranked[b].deadline);
  });
  return idx;
}

// ---------------------------------------------------------------------------
// Plan construction
// ---------------------------------------------------------------------------
RecoveryPlan RecoveryPlanner::Impl::build_plan(const RecoveryRequest& req,
                                               RankedCandidate& rc,
                                               Timestamp now,
                                               bool /*allow_violation*/) {
  const auto& c = rc.candidate;
  RecoveryPlan p;
  p.plan_id = RecoveryPlanId(next_plan_id_++);
  p.generation = RecoveryPlanGeneration(next_generation_++);
  p.request_id = req.request_id;
  p.workload = req.workload;
  p.strategy = c.strategy;
  p.candidate_id = c.candidate_id;
  p.candidate_generation = c.generation;
  p.target_worker = c.target_worker;
  p.target_boot = c.target_boot;
  p.target_engine = c.target_engine;
  p.engine_incarnation = c.target_engine_incarnation;
  p.state_source = c.state_source;
  p.state_generation = c.state_generation;
  p.checkpoint_source = c.checkpoint_source;

  PlanAuthority a;
  a.coordinator_epoch = ctx_.epoch;
  a.planner_boot = ctx_.planner_boot;
  a.request_id = req.request_id;
  a.plan_generation = p.generation;
  a.candidate_generation = c.generation;
  a.target_boot = c.target_boot;
  a.engine_incarnation = c.target_engine_incarnation;
  a.state_generation = c.state_generation;
  a.compatibility_generation = ctx_.compatibility_generation;
  a.resource_generation = ctx_.resource_generation;
  a.policy_generation = ctx_.policy_generation;
  a.topology_generation = ctx_.topology_generation;
  a.workload_generation = req.workload_generation;
  p.authority = a;

  p.state = PlanLifecycleState::PLANNED;
  p.economics = rc.economics;
  p.deadline = rc.deadline;
  p.dispatch_payload = build_payload(c);
  p.created_at = now;
  p.updated_at = now;
  p.decision_digest = decision_digest(req, p, ctx_);
  return p;
}

// ---------------------------------------------------------------------------
// Explanation
// ---------------------------------------------------------------------------
PlanExplanation RecoveryPlanner::Impl::build_explanation(
    const RecoveryRequest& req, const std::vector<RankedCandidate>& ranked,
    std::size_t selected, const std::vector<std::size_t>& /*eligible*/, Timestamp /*now*/) const {
  const RankedCandidate& rc = ranked[selected];
  const auto& c = rc.candidate;
  PlanExplanation ex;
  ex.selected_strategy = c.strategy;
  ex.selected_candidate = c.candidate_id;
  ex.selected_candidate_generation = c.generation;
  ex.authority.coordinator_epoch = ctx_.epoch;
  ex.authority.policy_generation = ctx_.policy_generation;

  // Rank factors (ordered, deterministic).
  ex.rank_factors.push_back({"expected_time",
                             value_or(rc.economics.expected_time, "unknown"), 1});
  ex.rank_factors.push_back({"expected_lost_progress",
                             value_or(rc.economics.expected_lost_progress, "unknown"), 2});
  ex.rank_factors.push_back({"cost_units",
                             value_or(rc.economics.cost_units, "unknown"), 3});
  ex.rank_factors.push_back({"failover_domain_risk",
                             std::to_string(rc.economics.failover_domain_risk), 4});
  ex.rank_factors.push_back({"confidence",
                             std::to_string(rc.economics.confidence.basis_points()), 5});

  ex.deadline_slack = rc.deadline.slack;
  ex.preserved_progress = rc.economics.expected_preserved_progress;
  ex.lost_progress = rc.economics.expected_lost_progress;
  ex.cost = rc.economics.cost_units;
  ex.readiness = c.availability ? c.availability->readiness : CandidateReadiness::UNKNOWN;
  ex.compatibility = c.compatibility ? c.compatibility->result : CompatibilityResult::UNKNOWN;
  ex.resource_status = c.resource ? c.resource->status : ResourceStatus::UNKNOWN;
  ex.uncertainty = c.authority ? c.authority->meta.confidence : Confidence::none();

  ex.binding_constraints = rc.feasibility.violations;
  // If the selected plan violates the deadline, record that explicitly.
  if (rc.deadline.outcome == DeadlineOutcome::VIOLATED) {
    ex.binding_constraints.push_back({RejectionReason::DEADLINE_IMPOSSIBLE,
                                      "selected fallback plan violates the recovery deadline"});
  }

  // Feasibility reasons (structured, deterministic summary of the winning path).
  if (rc.feasibility.feasible()) {
    ex.feasibility_reasons.push_back(std::string(to_string(c.strategy)) + " is feasible");
  }
  if (c.availability) {
    ex.feasibility_reasons.push_back("target worker " +
        std::to_string(c.availability->worker.value()) + " is READY under boot " +
        std::to_string(c.availability->boot.value()));
  }
  if (c.compatibility) {
    ex.feasibility_reasons.push_back(std::string("compatibility=") + to_string(c.compatibility->result));
  }
  if (c.resource) {
    ex.feasibility_reasons.push_back("resource '" + c.resource->resource_kind + "' is " +
        std::string(c.resource->status == ResourceStatus::AVAILABLE ? "available" : "not available"));
  }

  // Missing evidence.
  if (!c.state) ex.missing_evidence.push_back("state evidence");
  if (!c.availability) ex.missing_evidence.push_back("availability evidence");
  if (!c.resource) ex.missing_evidence.push_back("resource evidence");
  if (!c.compatibility) ex.missing_evidence.push_back("compatibility evidence");

  // Rejected alternatives.
  for (std::size_t i = 0; i < ranked.size(); ++i) {
    if (i == selected) continue;
    const auto& other = ranked[i];
    RejectedAlternative alt;
    alt.candidate_id = other.candidate.candidate_id;
    alt.strategy = other.candidate.strategy;
    alt.feasibility = other.feasibility.result;
    std::string why;
    if (other.feasibility.blocked() && other.feasibility.violations.size()) {
      why = to_string(other.feasibility.violations.front().reason);
    } else {
      why = to_string(other.feasibility.result);
    }
    alt.reasons.push_back(std::move(why));
    ex.rejected_alternatives.push_back(std::move(alt));
  }

  // What would change.
  ex.what_would_change = compute_what_would_change(req, ranked, /*plan placeholder*/ [&]{
    RecoveryPlan tmp;
    tmp.strategy = c.strategy;
    tmp.candidate_id = c.candidate_id;
    tmp.deadline = rc.deadline;
    tmp.economics = rc.economics;
    return tmp;
  }());
  return ex;
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------
void RecoveryPlanner::Impl::supersede_older(const RecoveryRequest& req, RecoveryPlanId keep, Timestamp now) {
  for (auto& kv : plans_) {
    RecoveryPlan& old = kv.second;
    if (old.plan_id == keep) continue;
    const auto s = old.state;
    const bool term = s == PlanLifecycleState::SUCCEEDED || s == PlanLifecycleState::FAILED ||
                      s == PlanLifecycleState::CANCELLED || s == PlanLifecycleState::EXPIRED ||
                      s == PlanLifecycleState::INVALIDATED || s == PlanLifecycleState::SUPERSEDED;
    if (old.request_id == req.request_id && !term) {
      if (validate_plan_transition(s, PlanLifecycleState::SUPERSEDED)) {
        old.state = PlanLifecycleState::SUPERSEDED;
        old.updated_at = now;
      }
    }
  }
}

void RecoveryPlanner::Impl::record_history(const RecoveryRequest& req,
                                           const RecoveryDecision& d) {
  std::ostringstream oss;
  oss << "req=" << req.request_id.value()
      << " workload=" << req.workload.value()
      << " outcome=" << to_string(d.outcome);
  if (d.plan) {
    oss << " plan=" << d.plan->plan_id.value()
        << " strategy=" << to_string(d.plan->strategy)
        << " candidate=" << d.plan->candidate_id.value()
        << " digest=" << d.plan->decision_digest;
  }
  history_.push_back(oss.str());
}

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------
RecoveryPlanner::RecoveryPlanner(std::shared_ptr<Clock> clock,
                                 std::shared_ptr<PersistenceStore> store)
    : impl_(new Impl(std::move(clock), std::move(store))) {}
RecoveryPlanner::~RecoveryPlanner() = default;

RecoveryDecision RecoveryPlanner::plan(const RecoveryRequest& request,
                                       std::vector<RecoveryCandidate> candidates) {
  std::lock_guard<std::mutex> lk(impl_->mu_);
  RecoveryDecision d;
  d.ranked.reserve(candidates.size());
  Timestamp now = impl_->now();

  if (candidates.empty()) {
    d.outcome = PlanOutcome::NO_CANDIDATES;
    d.reasons.push_back("no recovery candidates were supplied");
    return d;
  }

  for (std::size_t i = 0; i < candidates.size(); ++i) {
    RankedCandidate rc;
    rc.candidate = candidates[i];
    rc.feasibility = evaluate_feasibility(candidates[i], request, impl_->ctx_);
    rc.economics = compute_economics(candidates[i], request, impl_->ctx_, now);
    rc.deadline = evaluate_deadline(candidates[i], request, impl_->ctx_, now);
    rc.rank = static_cast<int>(i + 1);
    d.ranked.push_back(std::move(rc));
  }

  std::vector<std::size_t> feasible_idx;
  for (std::size_t i = 0; i < d.ranked.size(); ++i)
    if (d.ranked[i].feasibility.feasible()) feasible_idx.push_back(i);

  const bool hard_deadline = request.recovery_deadline.has_value() ||
                             request.max_recovery_duration.has_value();
  const bool hard_progress = request.max_tolerated_progress_loss.has_value();

  std::vector<std::size_t> eligible;
  for (std::size_t i : feasible_idx) {
    bool deadline_ok = true;
    if (hard_deadline) deadline_ok = d.ranked[i].deadline.outcome == DeadlineOutcome::HARD_FEASIBLE;
    bool progress_ok = true;
    if (hard_progress) progress_ok = d.ranked[i].economics.progress_loss_within_tolerance;
    if (deadline_ok && progress_ok) eligible.push_back(i);
  }

  if (eligible.empty()) {
    if (feasible_idx.empty()) {
      d.outcome = PlanOutcome::NO_VALID_PLAN;
      d.reasons.push_back("no candidate is feasible under current evidence");
      impl_->record_history(request, d);
      return d;
    }
    if (hard_deadline) {
      d.outcome = PlanOutcome::NO_DEADLINE_FEASIBLE_PLAN;
      d.reasons.push_back("no candidate satisfies the recovery deadline");
    } else {
      d.outcome = PlanOutcome::NO_VALID_PLAN;
      d.reasons.push_back("no candidate satisfies the hard constraints");
    }
    // Best fallback candidate, clearly labeled as violating the target.
    auto order = impl_->rank_feasible(d, feasible_idx);
    for (std::size_t k = 0; k < order.size(); ++k)
      d.ranked[order[k]].feasible_rank = static_cast<int>(k + 1);
    std::size_t best = order.front();
    d.plan = impl_->build_plan(request, d.ranked[best], now, true);
    impl_->plans_[d.plan->plan_id.value()] = *d.plan;
    impl_->supersede_older(request, d.plan->plan_id, now);
    impl_->record_history(request, d);
    return d;
  }

  auto order = impl_->rank_feasible(d, eligible);
  for (std::size_t k = 0; k < order.size(); ++k)
    d.ranked[order[k]].feasible_rank = static_cast<int>(k + 1);
  std::size_t best = order.front();

  d.plan = impl_->build_plan(request, d.ranked[best], now, false);
  d.plan->explanation = impl_->build_explanation(request, d.ranked, best, eligible, now);
  d.outcome = PlanOutcome::PLAN_FOUND;
  d.summary = std::string("selected ") + to_string(d.plan->strategy) +
              " on candidate " + std::to_string(d.plan->candidate_id.value());
  d.reasons.push_back("deterministic ranking selected " + std::string(to_string(d.plan->strategy)));

  impl_->plans_[d.plan->plan_id.value()] = *d.plan;
  impl_->requests_[request.request_id.value()] = request;
  impl_->supersede_older(request, d.plan->plan_id, now);
  impl_->record_history(request, d);
  return d;
}

RecoveryPlan RecoveryPlanner::authorize_plan(const RecoveryPlan& plan) {
  std::lock_guard<std::mutex> lk(impl_->mu_);
  auto it = impl_->plans_.find(plan.plan_id.value());
  if (it == impl_->plans_.end())
    throw RecoveryError(ErrorCode::NOT_FOUND, "plan not found");
  RecoveryPlan& stored = it->second;
  if (!plan_authority_current(stored, impl_->ctx_))
    throw RecoveryError(ErrorCode::STALE_AUTHORITY, "plan authority is stale");
  if (!validate_plan_transition(stored.state, PlanLifecycleState::AUTHORIZED))
    throw RecoveryError(ErrorCode::INVALID_INPUT, "cannot authorize from current lifecycle state");
  stored.state = PlanLifecycleState::AUTHORIZED;
  stored.updated_at = impl_->now();
  return stored;
}

RecoveryPlan RecoveryPlanner::revalidate_plan(const RecoveryPlan& plan) {
  std::lock_guard<std::mutex> lk(impl_->mu_);
  auto it = impl_->plans_.find(plan.plan_id.value());
  if (it == impl_->plans_.end())
    throw RecoveryError(ErrorCode::NOT_FOUND, "plan not found");
  const RecoveryPlan& stored = it->second;
  if (!plan_authority_current(stored, impl_->ctx_))
    throw RecoveryError(ErrorCode::STALE_AUTHORITY, "revalidation failed: plan authority is stale");
  // Deadline must still be viable.
  if (stored.deadline.outcome == DeadlineOutcome::VIOLATED)
    throw RecoveryError(ErrorCode::STALE_AUTHORITY, "revalidation failed: deadline no longer viable");
  return stored;
}

RecoveryPlan RecoveryPlanner::dispatch_plan(const RecoveryPlan& plan, IRecoveryExecutor& executor) {
  RecoveryDispatch dispatch;
  {
    std::lock_guard<std::mutex> lk(impl_->mu_);
    auto it = impl_->plans_.find(plan.plan_id.value());
    if (it == impl_->plans_.end())
      throw RecoveryError(ErrorCode::NOT_FOUND, "plan not found");
    RecoveryPlan& stored = it->second;
    if (!plan_authority_current(stored, impl_->ctx_))
      throw RecoveryError(ErrorCode::STALE_AUTHORITY, "plan authority is stale at dispatch");
    if (stored.state != PlanLifecycleState::AUTHORIZED) {
      const bool terminal = stored.state == PlanLifecycleState::SUCCEEDED ||
                            stored.state == PlanLifecycleState::FAILED ||
                            stored.state == PlanLifecycleState::CANCELLED ||
                            stored.state == PlanLifecycleState::EXPIRED ||
                            stored.state == PlanLifecycleState::INVALIDATED ||
                            stored.state == PlanLifecycleState::SUPERSEDED;
      if (terminal)
        throw RecoveryError(ErrorCode::STALE_AUTHORITY, "plan is no longer executable (stale/terminal)");
      throw RecoveryError(ErrorCode::INVALID_INPUT, "plan must be AUTHORIZED before dispatch");
    }
    if (!validate_plan_transition(stored.state, PlanLifecycleState::DISPATCHED))
      throw RecoveryError(ErrorCode::INVALID_INPUT, "cannot dispatch from current state");

    stored.updated_at = impl_->now();
    stored.last_attempt = AttemptId(stored.last_attempt.value() + 1);
    dispatch.dispatch_id = DispatchId(stored.last_attempt.value());
    dispatch.plan_id = stored.plan_id;
    dispatch.plan_generation = stored.generation;
    dispatch.request_id = stored.request_id;
    dispatch.strategy = stored.strategy;
    dispatch.target_worker = stored.target_worker;
    dispatch.target_boot = stored.target_boot;
    dispatch.engine_incarnation = stored.engine_incarnation;
    dispatch.state_source = stored.state_source;
    dispatch.state_generation = stored.state_generation;
    dispatch.attempt = stored.last_attempt;
    dispatch.payload = stored.dispatch_payload;
    stored.state = PlanLifecycleState::DISPATCHED;
    impl_->dispatch_meta_[dispatch.dispatch_id.value()] = {stored.plan_id, stored.generation, stored.target_boot};
    // Build the plan copy snapshot to hand to the executor outside the lock.
  }

  // Release the lock before the backend call (no network wait under a state lock).
  auto receipt = executor.dispatch(dispatch);

  std::lock_guard<std::mutex> lk(impl_->mu_);
  auto it = impl_->plans_.find(plan.plan_id.value());
  if (it == impl_->plans_.end())
    throw RecoveryError(ErrorCode::NOT_FOUND, "plan not found");
  RecoveryPlan& stored = it->second;
  // If the executor synchronously reported a terminal outcome, do not step back.
  if (stored.state == PlanLifecycleState::DISPATCHED &&
      receipt.status == DispatchStatus::DISPATCHED) {
    if (validate_plan_transition(stored.state, PlanLifecycleState::EXECUTING))
      stored.state = PlanLifecycleState::EXECUTING;
  } else if (receipt.status != DispatchStatus::DISPATCHED) {
    if (validate_plan_transition(stored.state, PlanLifecycleState::FAILED))
      stored.state = PlanLifecycleState::FAILED;
  }
  stored.updated_at = impl_->now();
  return stored;
}

void RecoveryPlanner::report_execution(const ExecutionReport& report) {
  std::lock_guard<std::mutex> lk(impl_->mu_);
  auto it = impl_->plans_.find(report.plan_id.value());
  if (it == impl_->plans_.end())
    return;  // unknown/stale plan id: ignore, never mutate current state
  RecoveryPlan& p = it->second;

  // Fencing: plan generation must match; reporter boot must match target boot.
  if (report.plan_generation != p.generation) return;
  if (report.reporter_boot != p.target_boot) return;

  // Terminal plans reject duplicate completion.
  const bool terminal = p.state == PlanLifecycleState::SUCCEEDED ||
                        p.state == PlanLifecycleState::FAILED ||
                        p.state == PlanLifecycleState::CANCELLED ||
                        p.state == PlanLifecycleState::EXPIRED ||
                        p.state == PlanLifecycleState::INVALIDATED ||
                        p.state == PlanLifecycleState::SUPERSEDED;
  if (terminal) return;

  ExecutionOutcome outcome = report.outcome;
  switch (outcome) {
    case ExecutionOutcome::SUCCEEDED:
      if (validate_plan_transition(p.state, PlanLifecycleState::SUCCEEDED)) {
        p.state = PlanLifecycleState::SUCCEEDED;
        p.execution_outcome = ExecutionOutcome::SUCCEEDED;
        p.outcome_published = true;
      }
      break;
    case ExecutionOutcome::FAILED:
      if (validate_plan_transition(p.state, PlanLifecycleState::FAILED)) {
        p.state = PlanLifecycleState::FAILED;
        p.execution_outcome = ExecutionOutcome::FAILED;
        p.outcome_published = true;
      }
      break;
    case ExecutionOutcome::AMBIGUOUS:
      // Ambiguous outcome: the plan must not silently become success. It stays
      // non-success; we record a failure terminal with outcome unknown and do
      // NOT mark it published (requires external resolution).
      if (validate_plan_transition(p.state, PlanLifecycleState::FAILED)) {
        p.state = PlanLifecycleState::FAILED;
        p.execution_outcome = ExecutionOutcome::AMBIGUOUS;
        p.outcome_published = false;
      }
      break;
    case ExecutionOutcome::UNKNOWN:
    case ExecutionOutcome::CANCELLED:
      if (validate_plan_transition(p.state, PlanLifecycleState::FAILED)) {
        p.state = PlanLifecycleState::FAILED;
        p.execution_outcome = outcome;
        p.outcome_published = false;
      }
      break;
  }
  p.updated_at = impl_->now();
}

void RecoveryPlanner::on_adapter_completion(DispatchId dispatch_id, ExecutionOutcome outcome,
                                             ProgressUnits recovered, Duration elapsed,
                                             std::string detail) {
  std::unique_lock<std::mutex> lk(impl_->mu_);
  auto it = impl_->dispatch_meta_.find(dispatch_id.value());
  if (it == impl_->dispatch_meta_.end()) return;  // unknown dispatch: ignore
  const auto& meta = it->second;
  ExecutionReport report;
  report.dispatch = dispatch_id;
  report.plan_id = std::get<0>(meta);
  report.plan_generation = std::get<1>(meta);
  report.reporter_boot = std::get<2>(meta);
  report.outcome = outcome;
  report.recovered = recovered;
  report.elapsed = elapsed;
  report.detail = std::move(detail);
  // Drop the dispatch metadata before delegating to report_execution.
  impl_->dispatch_meta_.erase(it);
  lk.unlock();
  report_execution(report);
}

bool RecoveryPlanner::cancel_plan(RecoveryPlanId plan_id, bool force) {
  std::lock_guard<std::mutex> lk(impl_->mu_);
  auto it = impl_->plans_.find(plan_id.value());
  if (it == impl_->plans_.end()) return false;
  RecoveryPlan& p = it->second;
  if (p.state == PlanLifecycleState::SUCCEEDED) return false;  // cannot cancel a completed recovery
  const bool dispatched = p.state == PlanLifecycleState::DISPATCHED ||
                          p.state == PlanLifecycleState::EXECUTING;
  if (dispatched && !force) return false;  // commit boundary not crossed unless forced
  if (dispatched && force && p.outcome_published) return false;
  if (validate_plan_transition(p.state, PlanLifecycleState::CANCELLED)) {
    p.state = PlanLifecycleState::CANCELLED;
    p.execution_outcome = ExecutionOutcome::CANCELLED;
    p.updated_at = impl_->now();
    return true;
  }
  return false;
}

void RecoveryPlanner::invalidate_plan(RecoveryPlanId plan_id) {
  std::lock_guard<std::mutex> lk(impl_->mu_);
  auto it = impl_->plans_.find(plan_id.value());
  if (it == impl_->plans_.end()) return;
  RecoveryPlan& p = it->second;
  if (p.state == PlanLifecycleState::SUCCEEDED) return;
  if (validate_plan_transition(p.state, PlanLifecycleState::INVALIDATED)) {
    p.state = PlanLifecycleState::INVALIDATED;
    p.updated_at = impl_->now();
  }
}

void RecoveryPlanner::register_worker(WorkerId w, WorkerBootId boot) {
  std::lock_guard<std::mutex> lk(impl_->mu_);
  impl_->ctx_.register_worker(w, boot);
}
void RecoveryPlanner::unregister_worker(WorkerId w) {
  std::lock_guard<std::mutex> lk(impl_->mu_);
  impl_->ctx_.unregister_worker(w);
}
void RecoveryPlanner::set_resource(const std::string& kind, std::uint64_t amount) {
  std::lock_guard<std::mutex> lk(impl_->mu_);
  impl_->ctx_.resource_available[kind] = amount;
}
void RecoveryPlanner::set_recompute_available(WorkloadGeneration gen, bool available) {
  std::lock_guard<std::mutex> lk(impl_->mu_);
  if (available) {
    impl_->ctx_.recompute_lineage_available.insert(gen);
    impl_->ctx_.original_inputs_available.insert(gen);
  } else {
    impl_->ctx_.recompute_lineage_available.erase(gen);
    impl_->ctx_.original_inputs_available.erase(gen);
  }
}

bool RecoveryPlanner::save_state() {
  std::lock_guard<std::mutex> lk(impl_->mu_);
  if (!impl_->store_) return false;
  impl_->save_locked();
  return true;
}
bool RecoveryPlanner::load_state() {
  std::lock_guard<std::mutex> lk(impl_->mu_);
  if (!impl_->store_) return false;
  impl_->load_locked();
  return true;
}
void RecoveryPlanner::restart_coordinator() {
  std::lock_guard<std::mutex> lk(impl_->mu_);
  if (impl_->store_) impl_->save_locked();
  // Advance coordinator epoch and boot.
  impl_->ctx_.epoch = impl_->ctx_.epoch.next();
  impl_->ctx_.planner_boot = impl_->ctx_.planner_boot.next();
  // Dynamic evidence is invalid until re-registered/re-published.
  impl_->ctx_.alive_workers.clear();
  impl_->ctx_.resource_available.clear();
  impl_->ctx_.recompute_lineage_available.clear();
  impl_->ctx_.original_inputs_available.clear();
  if (impl_->store_) {
    auto blob = impl_->store_->load("planner_state");
    if (blob) impl_->apply_snapshot(*blob, true);
  }
  // Conservative: any non-terminal plan becomes INVALIDATED (must be replanned).
  for (auto& kv : impl_->plans_) {
    RecoveryPlan& p = kv.second;
    const auto st = p.state;
    const bool terminal = st == PlanLifecycleState::SUCCEEDED ||
                          st == PlanLifecycleState::FAILED ||
                          st == PlanLifecycleState::CANCELLED ||
                          st == PlanLifecycleState::EXPIRED ||
                          st == PlanLifecycleState::INVALIDATED ||
                          st == PlanLifecycleState::SUPERSEDED;
    if (!terminal) {
      p.state = PlanLifecycleState::INVALIDATED;
      p.updated_at = impl_->now();
    }
  }
  // Mark worker registrations as requiring revalidation (dynamic evidence).
  for (auto& kv : impl_->ctx_.worker_boots) {
    (void)kv;
  }
  impl_->history_.push_back("coordinator restart; epoch=" + std::to_string(impl_->ctx_.epoch.value()) +
                            " boot=" + std::to_string(impl_->ctx_.planner_boot.value()));
}

const PlannerContext& RecoveryPlanner::context() const { return impl_->ctx_; }
CoordinatorEpoch RecoveryPlanner::current_epoch() const { return impl_->ctx_.epoch; }
std::vector<RecoveryPlan> RecoveryPlanner::plans() const {
  std::lock_guard<std::mutex> lk(impl_->mu_);
  std::vector<RecoveryPlan> out;
  for (const auto& kv : impl_->plans_) out.push_back(kv.second);
  return out;
}
std::size_t RecoveryPlanner::history_size() const { return impl_->history_.size(); }
std::vector<std::string> RecoveryPlanner::history_summaries() const { return impl_->history_; }

}  // namespace recovery_planner
