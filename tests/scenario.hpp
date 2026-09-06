#pragma once

// Deterministic test scenario builders. These construct canonical contexts,
// requests and candidates used throughout the test suite. All evidence is
// CURRENT and feasible by default; tests mutate specific fields to create
// staleness, incompatibility, resource pressure, etc.

#include "recovery_planner/recovery_planner.hpp"

namespace scenario {

using namespace recovery_planner;

constexpr CoordinatorEpoch kEpoch = CoordinatorEpoch(1);
constexpr PlannerBootId kBoot = PlannerBootId(1);
constexpr WorkloadGeneration kWorkloadGen = WorkloadGeneration(1);
constexpr PolicyGeneration kPolicyGen = PolicyGeneration(1);
constexpr CompatibilityGeneration kCompatGen = CompatibilityGeneration(1);
constexpr ResourceGeneration kResourceGen = ResourceGeneration(1);
constexpr TopologyGeneration kTopoGen = TopologyGeneration(1);

constexpr WorkerId kWorkerA = WorkerId(1);
constexpr WorkerBootId kBootA = WorkerBootId(100);
constexpr WorkerId kWorkerB = WorkerId(2);
constexpr WorkerBootId kBootB = WorkerBootId(200);
constexpr EngineId kEngineA = EngineId(1);
constexpr EngineIncarnationId kEngineIncA = EngineIncarnationId(10);
constexpr StateId kStateA = StateId(1000);
constexpr StateGeneration kStateGenA = StateGeneration(1);
constexpr CheckpointId kCheckpointA = CheckpointId(5000);

// ---- contexts --------------------------------------------------------------

inline PlannerContext make_context() {
  PlannerContext ctx;
  ctx.epoch = kEpoch;
  ctx.planner_boot = kBoot;
  ctx.workload_generation = kWorkloadGen;
  ctx.policy_generation = kPolicyGen;
  ctx.compatibility_generation = kCompatGen;
  ctx.resource_generation = kResourceGen;
  ctx.topology_generation = kTopoGen;
  ctx.register_worker(kWorkerA, kBootA);
  ctx.register_worker(kWorkerB, kBootB);
  ctx.resource_available["gpu_mem"] = 10000;
  ctx.resource_available["host_mem"] = 200000;
  ctx.recompute_lineage_available.insert(kWorkloadGen);
  ctx.original_inputs_available.insert(kWorkloadGen);
  ctx.compatibility_table["workload:v1"] = {"workerA", "workerB", "gpu:sm120"};
  return ctx;
}

inline void register_workers(PlannerContext& ctx) {
  ctx.register_worker(kWorkerA, kBootA);
  ctx.register_worker(kWorkerB, kBootB);
}

// ---- requests --------------------------------------------------------------

inline RecoveryRequest make_request(std::string reason = "worker lost") {
  RecoveryRequest r;
  r.request_id = RecoveryRequestId(1);
  r.workload = WorkloadId(1);
  r.workload_generation = kWorkloadGen;
  r.execution = ExecutionId(1);
  r.execution_generation = ExecutionGeneration(1);
  r.failure = FailureId(1);
  r.reason = std::move(reason);
  r.coordinator_epoch = kEpoch;
  r.policy_generation = kPolicyGen;
  r.topology_generation = kTopoGen;
  r.requested_at = Timestamp(0);
  return r;
}

// ---- candidates ------------------------------------------------------------

// Build a candidate that is feasible by default under make_context().
inline RecoveryCandidate make_candidate(RecoveryStrategy strategy, std::uint64_t id,
                                        WorkloadGeneration wg = kWorkloadGen) {
  RecoveryCandidate c;
  c.candidate_id = CandidateId(id);
  c.generation = CandidateGeneration(id);
  c.strategy = strategy;
  c.target_worker = kWorkerA;
  c.target_boot = kBootA;
  c.target_engine = kEngineA;
  c.target_engine_incarnation = kEngineIncA;
  c.source_worker = (strategy == RecoveryStrategy::MIGRATE) ? kWorkerB : kWorkerA;
  c.source_boot = (strategy == RecoveryStrategy::MIGRATE) ? kBootB : kBootA;
  c.state_source = kStateA;
  c.state_generation = kStateGenA;
  c.checkpoint_source = kCheckpointA;
  c.checkpoint_generation = CheckpointGeneration(1);
  c.reason_for_existence = "scenario candidate " + std::to_string(id);

  StateEvidence st;
  st.meta.coordinator_epoch = kEpoch;
  st.meta.observer = kBootA;
  st.meta.observed_at = Timestamp(100);
  st.meta.provenance = EvidenceProvenance::MEASURED;
  st.meta.freshness = Freshness::CURRENT;
  st.meta.confidence = Confidence::full();
  st.state_id = kStateA;
  st.state_generation = kStateGenA;
  st.workload_generation = wg;
  st.checkpoint_generation = CheckpointGeneration(1);
  st.integrity = StateIntegrity::VALID;
  st.size_bytes = ByteCount(1024);
  st.restore_bytes = ByteCount(1024);
  st.preserved_progress = ProgressUnits(90);
  st.durable_progress = ProgressUnits(90);
  st.location = StorageLocation::LOCAL;
  st.is_durable = true;
  st.identity = "workload:v1";
  c.state = st;

  CompatibilityEvidence co;
  co.meta.coordinator_epoch = kEpoch;
  co.meta.observer = kBootA;
  co.meta.observed_at = Timestamp(100);
  co.meta.provenance = EvidenceProvenance::REPORTED;
  co.meta.freshness = Freshness::CURRENT;
  co.compatibility_generation = kCompatGen;
  co.result = CompatibilityResult::EXACT;
  co.subject_identity = "workload:v1";
  co.state_format = "workerA";
  co.reason = "exact match";
  c.compatibility = co;

  AvailabilityEvidence av;
  av.meta.coordinator_epoch = kEpoch;
  av.meta.observer = kBootA;
  av.meta.observed_at = Timestamp(100);
  av.meta.provenance = EvidenceProvenance::MEASURED;
  av.meta.freshness = Freshness::CURRENT;
  av.worker = kWorkerA;
  av.boot = kBootA;
  av.engine_incarnation = kEngineIncA;
  av.is_alive = true;
  av.readiness = CandidateReadiness::READY;
  c.availability = av;

  ResourceEvidence re;
  re.meta.coordinator_epoch = kEpoch;
  re.meta.observer = kBootA;
  re.meta.observed_at = Timestamp(100);
  re.meta.provenance = EvidenceProvenance::MEASURED;
  re.meta.freshness = Freshness::CURRENT;
  re.snapshot = ResourceSnapshotId(1);
  re.generation = kResourceGen;
  re.resource_kind = "gpu_mem";
  re.required = 100;
  re.available = 10000;
  re.status = ResourceStatus::AVAILABLE;
  c.resource = re;

  TopologyEvidence tp;
  tp.meta.coordinator_epoch = kEpoch;
  tp.meta.observer = kBootA;
  tp.meta.observed_at = Timestamp(100);
  tp.meta.provenance = EvidenceProvenance::MEASURED;
  tp.meta.freshness = Freshness::CURRENT;
  tp.generation = kTopoGen;
  tp.path_possible = true;
  tp.source = "workerA";
  tp.destination = "workerB";
  c.topology = tp;

  TransferEvidence tr;
  tr.meta.coordinator_epoch = kEpoch;
  tr.meta.observer = kBootA;
  tr.meta.observed_at = Timestamp(100);
  tr.meta.provenance = EvidenceProvenance::ESTIMATED;
  tr.meta.freshness = Freshness::CURRENT;
  tr.transfer_bytes = ByteCount(1024);
  tr.bandwidth = Bandwidth(1024 * 1024);
  tr.from = "workerA";
  tr.to = "workerB";
  c.transfer = tr;

  CheckpointEvidence cp;
  cp.meta.coordinator_epoch = kEpoch;
  cp.meta.observer = kBootA;
  cp.meta.observed_at = Timestamp(100);
  cp.meta.provenance = EvidenceProvenance::MEASURED;
  cp.meta.freshness = Freshness::CURRENT;
  cp.checkpoint_id = kCheckpointA;
  cp.checkpoint_generation = CheckpointGeneration(1);
  cp.state_generation = kStateGenA;
  cp.age = Duration(1000);
  cp.integrity = StateIntegrity::VALID;
  cp.is_durable = true;
  c.checkpoint = cp;

  ShadowEvidence sh;
  sh.meta.coordinator_epoch = kEpoch;
  sh.meta.observer = kBootA;
  sh.meta.observed_at = Timestamp(100);
  sh.meta.provenance = EvidenceProvenance::MEASURED;
  sh.meta.freshness = Freshness::CURRENT;
  sh.shadow_worker = kWorkerB;
  sh.shadow_boot = kBootB;
  sh.shadow_state_generation = kStateGenA;
  sh.lag = Duration(50);
  sh.last_sync_point = Timestamp(90);
  sh.readiness = CandidateReadiness::READY;
  sh.compatibility = CompatibilityResult::EXACT;
  c.shadow = sh;

  ProgressEvidence pr;
  pr.meta.coordinator_epoch = kEpoch;
  pr.meta.observer = kBootA;
  pr.meta.observed_at = Timestamp(100);
  pr.meta.provenance = EvidenceProvenance::MEASURED;
  pr.meta.freshness = Freshness::CURRENT;
  pr.committed = ProgressUnits(100);
  pr.durable = ProgressUnits(90);
  pr.recoverable = ProgressUnits(90);
  pr.recomputable = ProgressUnits(80);
  pr.lost = ProgressUnits(0);
  pr.total = ProgressUnits(100);
  c.progress = pr;

  CostEvidence cost;
  cost.meta.coordinator_epoch = kEpoch;
  cost.meta.observer = kBootA;
  cost.meta.observed_at = Timestamp(100);
  cost.meta.provenance = EvidenceProvenance::ESTIMATED;
  cost.meta.freshness = Freshness::CURRENT;
  cost.expected_time = Duration(5000);        // 5 microseconds? use us units
  cost.restore_time = Duration(3000);
  cost.restart_time = Duration(1000);
  cost.transfer_time = Duration(1000);
  cost.recompute_time = Duration(500);
  cost.bytes_transferred = ByteCount(1024);
  cost.bytes_read = ByteCount(1024);
  cost.bytes_materialized = ByteCount(1024);
  cost.expected_lost_progress = ProgressUnits(0);
  cost.expected_preserved_progress = ProgressUnits(90);
  c.cost = cost;

  AuthorityEvidence auth;
  auth.meta.coordinator_epoch = kEpoch;
  auth.meta.observer = kBootA;
  auth.meta.observed_at = Timestamp(100);
  auth.meta.provenance = EvidenceProvenance::MEASURED;
  auth.meta.freshness = Freshness::CURRENT;
  auth.meta.confidence = Confidence::full();
  auth.coordinator_epoch = kEpoch;
  auth.policy_generation = kPolicyGen;
  auth.coordinator_authority_valid = true;
  auth.policy_authority_valid = true;
  auth.candidate_authority_valid = true;
  auth.execution_authority_valid = true;
  c.authority = auth;

  return c;
}

}  // namespace scenario
