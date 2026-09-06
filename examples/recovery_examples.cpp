#include "recovery_planner/recovery_planner.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace recovery_planner;

namespace {

RecoveryCandidate cand(RecoveryStrategy s, std::uint64_t id, Duration time,
                       ProgressUnits preserved, ProgressUnits lost) {
  RecoveryCandidate c;
  c.candidate_id = CandidateId(id);
  c.generation = CandidateGeneration(id);
  c.strategy = s;
  c.target_worker = WorkerId(1); c.target_boot = WorkerBootId(100);
  c.target_engine = EngineId(1); c.target_engine_incarnation = EngineIncarnationId(1);
  c.source_worker = WorkerId(1); c.source_boot = WorkerBootId(100);
  c.state_source = StateId(1000); c.state_generation = StateGeneration(1);
  c.checkpoint_source = CheckpointId(5000); c.checkpoint_generation = CheckpointGeneration(1);

  StateEvidence st;
  st.meta.coordinator_epoch = CoordinatorEpoch(1); st.meta.observer = WorkerBootId(100);
  st.meta.observed_at = Timestamp(100); st.meta.provenance = EvidenceProvenance::MEASURED;
  st.meta.freshness = Freshness::CURRENT; st.meta.confidence = Confidence::full();
  st.state_id = StateId(1000); st.state_generation = StateGeneration(1);
  st.workload_generation = WorkloadGeneration(1); st.checkpoint_generation = CheckpointGeneration(1);
  st.integrity = StateIntegrity::VALID; st.size_bytes = ByteCount(1024); st.restore_bytes = ByteCount(1024);
  st.preserved_progress = preserved; st.durable_progress = preserved;
  st.location = StorageLocation::LOCAL; st.is_durable = true; st.identity = "workload:v1";
  c.state = st;

  CompatibilityEvidence co;
  co.meta.coordinator_epoch = CoordinatorEpoch(1); co.meta.observer = WorkerBootId(100);
  co.meta.observed_at = Timestamp(100); co.meta.provenance = EvidenceProvenance::REPORTED;
  co.meta.freshness = Freshness::CURRENT; co.compatibility_generation = CompatibilityGeneration(1);
  co.result = CompatibilityResult::EXACT; co.subject_identity = "workload:v1"; co.state_format = "worker:1";
  c.compatibility = co;

  AvailabilityEvidence av;
  av.meta.coordinator_epoch = CoordinatorEpoch(1); av.meta.observer = WorkerBootId(100);
  av.meta.observed_at = Timestamp(100); av.meta.provenance = EvidenceProvenance::MEASURED;
  av.meta.freshness = Freshness::CURRENT; av.worker = WorkerId(1); av.boot = WorkerBootId(100);
  av.engine_incarnation = EngineIncarnationId(1); av.is_alive = true; av.readiness = CandidateReadiness::READY;
  c.availability = av;

  ResourceEvidence re;
  re.meta.coordinator_epoch = CoordinatorEpoch(1); re.meta.observer = WorkerBootId(100);
  re.meta.observed_at = Timestamp(100); re.meta.provenance = EvidenceProvenance::MEASURED;
  re.meta.freshness = Freshness::CURRENT; re.snapshot = ResourceSnapshotId(1);
  re.generation = ResourceGeneration(1); re.resource_kind = "gpu_mem"; re.required = 100;
  re.available = 10000; re.status = ResourceStatus::AVAILABLE;
  c.resource = re;

  TopologyEvidence tp;
  tp.meta.coordinator_epoch = CoordinatorEpoch(1); tp.meta.observer = WorkerBootId(100);
  tp.meta.observed_at = Timestamp(100); tp.meta.provenance = EvidenceProvenance::MEASURED;
  tp.meta.freshness = Freshness::CURRENT; tp.generation = TopologyGeneration(1); tp.path_possible = true;
  tp.source = "worker:1"; tp.destination = "worker:2"; c.topology = tp;

  TransferEvidence tr; tr.meta.coordinator_epoch = CoordinatorEpoch(1); tr.transfer_bytes = ByteCount(1024);
  c.transfer = tr;

  CostEvidence cost;
  cost.meta.coordinator_epoch = CoordinatorEpoch(1); cost.meta.observer = WorkerBootId(100);
  cost.meta.observed_at = Timestamp(100); cost.meta.provenance = EvidenceProvenance::ESTIMATED;
  cost.meta.freshness = Freshness::CURRENT; cost.expected_time = time;
  cost.expected_lost_progress = lost; cost.expected_preserved_progress = preserved;
  cost.bytes_transferred = ByteCount(1024); cost.bytes_read = ByteCount(1024); cost.bytes_materialized = ByteCount(1024);
  if (s == RecoveryStrategy::RESTORE) cost.restore_time = time;
  if (s == RecoveryStrategy::RECOMPUTE) cost.recompute_time = time;
  if (s == RecoveryStrategy::MIGRATE) cost.transfer_time = time;
  c.cost = cost;

  ShadowEvidence sh;
  sh.meta.coordinator_epoch = CoordinatorEpoch(1); sh.meta.observer = WorkerBootId(100);
  sh.meta.observed_at = Timestamp(100); sh.meta.provenance = EvidenceProvenance::MEASURED;
  sh.meta.freshness = Freshness::CURRENT; sh.shadow_worker = WorkerId(1); sh.shadow_boot = WorkerBootId(100);
  sh.shadow_state_generation = StateGeneration(1); sh.lag = Duration(25); sh.last_sync_point = Timestamp(90);
  sh.readiness = CandidateReadiness::READY; sh.compatibility = CompatibilityResult::EXACT;
  c.shadow = sh;

  AuthorityEvidence auth;
  auth.meta.coordinator_epoch = CoordinatorEpoch(1); auth.meta.observer = WorkerBootId(100);
  auth.meta.observed_at = Timestamp(100); auth.meta.provenance = EvidenceProvenance::MEASURED;
  auth.meta.freshness = Freshness::CURRENT; auth.meta.confidence = Confidence::full();
  auth.coordinator_epoch = CoordinatorEpoch(1); auth.policy_generation = PolicyGeneration(1);
  auth.coordinator_authority_valid = true; auth.policy_authority_valid = true;
  auth.candidate_authority_valid = true; auth.execution_authority_valid = true;
  c.authority = auth;
  return c;
}

RecoveryRequest req_for() {
  RecoveryRequest r; r.request_id = RecoveryRequestId(1); r.workload = WorkloadId(1);
  r.workload_generation = WorkloadGeneration(1); r.execution = ExecutionId(1);
  r.execution_generation = ExecutionGeneration(1); r.failure = FailureId(1); r.reason = "worker lost";
  r.coordinator_epoch = CoordinatorEpoch(1); r.policy_generation = PolicyGeneration(1);
  r.topology_generation = TopologyGeneration(1); r.requested_at = Timestamp(0);
  return r;
}

void demo(const char* title, RecoveryRequest req, std::vector<RecoveryCandidate> cands) {
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  planner.register_worker(WorkerId(1), WorkerBootId(100));
  planner.register_worker(WorkerId(2), WorkerBootId(200));
  planner.set_resource("gpu_mem", 10000);
  planner.set_recompute_available(WorkloadGeneration(1), true);
  auto d = planner.plan(req, cands);
  std::printf("%-32s outcome=%-14s selected=%s\n", title, to_string(d.outcome),
              d.plan ? to_string(d.plan->strategy) : "-");
}

}  // namespace

int main() {
  // Example 1: simple restore-vs-restart choice (restore preserves more).
  auto restore = cand(RecoveryStrategy::RESTORE, 11, Duration(3000), ProgressUnits(100), ProgressUnits(0));
  auto restart = cand(RecoveryStrategy::RESTART, 12, Duration(1000), ProgressUnits(0), ProgressUnits(100));
  demo("restore_vs_restart", req_for(), {restore, restart});

  // Example 2: deadline-driven recomputation (tight deadline -> recompute).
  auto restore_slow = cand(RecoveryStrategy::RESTORE, 11, Duration(500000), ProgressUnits(100), ProgressUnits(0));
  auto recompute = cand(RecoveryStrategy::RECOMPUTE, 13, Duration(1000), ProgressUnits(90), ProgressUnits(10));
  RecoveryRequest tight = req_for(); tight.recovery_deadline = Timestamp(5000);
  demo("deadline_loose_picks_restore", req_for(), {restore_slow, recompute});
  demo("deadline_tight_picks_recompute", tight, {restore_slow, recompute});

  // Example 3: stale candidate rejection.
  auto stale = cand(RecoveryStrategy::RESTORE, 11, Duration(1000), ProgressUnits(100), ProgressUnits(0));
  stale.authority->coordinator_epoch = CoordinatorEpoch(999);
  stale.authority->meta.coordinator_epoch = CoordinatorEpoch(999);
  demo("stale_candidate_rejected", req_for(), {stale});
  (void)restart;

  // Example 4: incompatible migration.
  auto mig = cand(RecoveryStrategy::MIGRATE, 21, Duration(2000), ProgressUnits(95), ProgressUnits(5));
  mig.compatibility->result = CompatibilityResult::INCOMPATIBLE;
  demo("incompatible_migration", req_for(), {mig});

  // Example 5: shadow promotion.
  auto shadow = cand(RecoveryStrategy::SHADOW_PROMOTE, 31, Duration(50), ProgressUnits(100), ProgressUnits(0));
  auto slow_restore = cand(RecoveryStrategy::RESTORE, 11, Duration(3000), ProgressUnits(100), ProgressUnits(0));
  demo("shadow_promotion", req_for(), {slow_restore, shadow});

  // Example 6: rehydration.
  auto rehydrate = cand(RecoveryStrategy::REHYDRATE, 41, Duration(1500), ProgressUnits(90), ProgressUnits(10));
  demo("rehydration", req_for(), {rehydrate});

  // Example 7: plan explanation.
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  planner.register_worker(WorkerId(1), WorkerBootId(100));
  planner.register_worker(WorkerId(2), WorkerBootId(200));
  planner.set_resource("gpu_mem", 10000);
  planner.set_recompute_available(WorkloadGeneration(1), true);
  auto d = planner.plan(req_for(), {slow_restore, shadow});
  if (d.plan) {
    std::printf("plan_explanation: strategy=%s candidate=%llu digest=%s rejected=%zu what_changes=%zu\n",
                to_string(d.plan->strategy), (unsigned long long)d.plan->candidate_id.value(),
                d.plan->decision_digest.c_str(), d.plan->explanation.rejected_alternatives.size(),
                d.plan->explanation.what_would_change.conditions.size());
  }

  // Example 8: coordinator restart invalidates an in-flight plan.
  auto store = make_memory_persistence_store();
  auto clock2 = std::make_shared<TestClock>(Timestamp(0));
  std::shared_ptr<PersistenceStore> store2 = store;
  RecoveryPlanner planner2(clock2, store2);
  planner2.register_worker(WorkerId(1), WorkerBootId(100));
  planner2.register_worker(WorkerId(2), WorkerBootId(200));
  planner2.set_resource("gpu_mem", 10000);
  planner2.set_recompute_available(WorkloadGeneration(1), true);
  auto d2 = planner2.plan(req_for(), {slow_restore});
  if (d2.plan) planner2.authorize_plan(*d2.plan);
  auto before = planner2.current_epoch().value();
  planner2.restart_coordinator();
  auto after = planner2.current_epoch().value();
  bool invalidated = false;
  for (const auto& p : planner2.plans())
    if (p.state == PlanLifecycleState::INVALIDATED) invalidated = true;
  std::printf("coordinator_restart: epoch=%llu->%llu inflight_invalidated=%s\n",
              (unsigned long long)before, (unsigned long long)after, invalidated ? "YES" : "NO");

  // Example 9: no valid plan outcome.
  auto stale2 = cand(RecoveryStrategy::RESTART, 12, Duration(1000), ProgressUnits(0), ProgressUnits(100));
  stale2.authority->coordinator_epoch = CoordinatorEpoch(999);
  stale2.authority->meta.coordinator_epoch = CoordinatorEpoch(999);
  demo("no_valid_plan", req_for(), {stale2});

  return 0;
}
