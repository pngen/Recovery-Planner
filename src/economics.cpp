#include "recovery_planner/economics.hpp"

#include <algorithm>
#include <limits>

namespace recovery_planner {

namespace {

// Compare two optionals where "lower is better". Unknown (nullopt) is treated as
// the worst possible value. Returns negative if a < b (a better), positive if a
// > b, zero if equal. This keeps ordering stable and never treats missing cost
// as zero.
template <typename T>
int cmp_lower_better(const std::optional<T>& a, const std::optional<T>& b) {
  if (a.has_value() && b.has_value()) {
    if (*a < *b) return -1;
    if (*b < *a) return 1;
    return 0;
  }
  if (a.has_value()) return -1;   // known beats unknown
  if (b.has_value()) return 1;    // known beats unknown
  return 0;                       // both unknown
}

// Compare two optionals where "higher is better"; unknown treated as worst.
template <typename T>
int cmp_higher_better(const std::optional<T>& a, const std::optional<T>& b) {
  return cmp_lower_better(b, a);
}

int cmp_u8(long long a, long long b) {
  return a < b ? -1 : (b < a ? 1 : 0);
}

std::int64_t sat_add(std::int64_t a, std::int64_t b) {
  if (b > 0 && a > INT64_MAX - b) return INT64_MAX;
  if (b < 0 && a < INT64_MIN - b) return INT64_MIN;
  return a + b;
}
std::int64_t sat_sub(std::int64_t a, std::int64_t b) {
  if (b > 0 && a < INT64_MIN + b) return INT64_MIN;
  if (b < 0 && a > INT64_MAX + b) return INT64_MAX;
  return a - b;
}

}  // namespace

DeadlineEvaluation evaluate_deadline(const RecoveryCandidate& candidate,
                                     const RecoveryRequest& request,
                                     const PlannerContext& /*context*/,
                                     Timestamp now) {
  DeadlineEvaluation d;
  if (request.recovery_deadline.has_value()) {
    d.deadline = *request.recovery_deadline;
  } else if (request.max_recovery_duration.has_value()) {
    d.deadline = Timestamp(now.value() + request.max_recovery_duration->value());
  } else {
    d.outcome = DeadlineOutcome::NO_DEADLINE;
    return d;
  }

  std::optional<Duration> expected;
  if (candidate.cost && candidate.cost->expected_time.has_value()) expected = *candidate.cost->expected_time;

  if (!expected.has_value()) {
    d.outcome = DeadlineOutcome::UNKNOWN;
    return d;
  }

  std::int64_t exp = expected->value();
  if (exp < 0) exp = 0;   // negative durations are not meaningful; clamp to 0
  std::int64_t comp = sat_add(now.value(), exp);
  std::int64_t dl = d.deadline->value();
  // A deadline in the past is effectively "now": completion cannot be before now.
  if (dl < now.value()) dl = now.value();
  d.expected_completion = Timestamp(comp);
  d.slack = Duration(sat_sub(dl, comp));
  d.outcome = d.slack->value() >= 0 ? DeadlineOutcome::HARD_FEASIBLE : DeadlineOutcome::VIOLATED;
  return d;
}

RecoveryEconomics compute_economics(const RecoveryCandidate& candidate,
                                    const RecoveryRequest& request,
                                    const PlannerContext& context,
                                    Timestamp now) {
  RecoveryEconomics e;
  const auto& cost = candidate.cost;
  if (cost) {
    e.expected_time = cost->expected_time;
    e.restore_time = cost->restore_time;
    e.restart_time = cost->restart_time;
    e.transfer_duration = cost->transfer_time;
    e.recompute_time = cost->recompute_time;
    // rehydrate_time is intentionally unknown unless evidence provides it.
    e.transfer_bytes = cost->bytes_transferred;
    e.restore_bytes = cost->bytes_read;
    e.materialize_bytes = cost->bytes_materialized;
    e.expected_lost_progress = cost->expected_lost_progress;
    e.expected_preserved_progress = cost->expected_preserved_progress;
    e.cost_units = cost->cost_units;
    e.checkpoint_age = cost->checkpoint_age;
    e.failover_domain_risk = cost->failover_domain_risk;
  }

  // Checkpoint age from state evidence if present.
  if (candidate.checkpoint && candidate.checkpoint->meta.freshness == Freshness::CURRENT) {
    e.checkpoint_age = candidate.checkpoint->age;
  } else if (candidate.checkpoint) {
    e.checkpoint_age = candidate.checkpoint->age;
  }
  if (!e.checkpoint_age && candidate.state) {
    // derive age from observation timestamp where possible
    if (candidate.state->meta.provenance != EvidenceProvenance::UNKNOWN) {
      e.checkpoint_age = Duration(0);
    }
  }

  // Deadline slack and feasibility.
  const DeadlineEvaluation de = evaluate_deadline(candidate, request, context, now);
  e.deadline_slack = de.slack;

  // Progress-loss tolerance.
  if (request.max_tolerated_progress_loss.has_value() && e.expected_lost_progress.has_value()) {
    e.max_tolerated_progress_loss = *request.max_tolerated_progress_loss;
    e.progress_loss_within_tolerance = e.expected_lost_progress->value() <= request.max_tolerated_progress_loss->value();
  }

  e.confidence = (cost && candidate.authority) ? candidate.authority->meta.confidence : Confidence::none();
  return e;
}

bool candidate_precedes(const RecoveryCandidate& lhs, const RecoveryCandidate& rhs,
                        const RecoveryEconomics& le, const RecoveryEconomics& re,
                        const DeadlineEvaluation& /*ld*/, const DeadlineEvaluation& /*rd*/) {
  // Deterministic lexicographic ordering over named objectives. The recovery
  // objective prioritizes preserved progress, then the economics of reaching it.
  // Priority order:
  //   1. preserved progress (higher better)  -- the core recovery objective
  //   2. lost progress (lower better)
  //   3. expected recovery time (lower better)
  //   4. cost units (lower better)
  //   5. failover domain risk (lower better)
  //   6. confidence (higher better)
  //   7. candidate id (lower) then generation (lower), as a stable final tie-break
  int cmp = cmp_higher_better(le.expected_preserved_progress, re.expected_preserved_progress);
  if (cmp != 0) return cmp < 0;
  cmp = cmp_lower_better(le.expected_lost_progress, re.expected_lost_progress);
  if (cmp != 0) return cmp < 0;
  cmp = cmp_lower_better(le.expected_time, re.expected_time);
  if (cmp != 0) return cmp < 0;
  cmp = cmp_lower_better(le.cost_units, re.cost_units);
  if (cmp != 0) return cmp < 0;
  cmp = cmp_u8(le.failover_domain_risk, re.failover_domain_risk);
  if (cmp != 0) return cmp < 0;
  cmp = cmp_u8(le.confidence.basis_points(), re.confidence.basis_points());
  if (cmp != 0) return cmp > 0;   // higher confidence is better
  // Final deterministic tie-break: candidate id, then generation, then strategy.
  if (lhs.candidate_id != rhs.candidate_id) return lhs.candidate_id < rhs.candidate_id;
  if (lhs.generation != rhs.generation) return lhs.generation < rhs.generation;
  if (lhs.strategy != rhs.strategy) return lhs.strategy < rhs.strategy;
  return false;   // fully equivalent: neither precedes the other
}

}  // namespace recovery_planner
