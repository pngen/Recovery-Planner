#include "recovery_planner/recovery_planner.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace recovery_planner;

namespace {

// Build a candidate for a strategy with tuned economics for the CLI scenarios.
RecoveryCandidate make_candidate(RecoveryStrategy s, std::uint64_t id, Duration time,
                                 ProgressUnits preserved, ProgressUnits lost,
                                 bool feasible = true) {
  RecoveryCandidate c;
  c.candidate_id = CandidateId(id);
  c.generation = CandidateGeneration(id);
  c.strategy = s;
  c.target_worker = WorkerId(1);
  c.target_boot = WorkerBootId(100);
  c.target_engine = EngineId(1);
  c.target_engine_incarnation = EngineIncarnationId(1);
  c.source_worker = WorkerId(1);
  c.source_boot = WorkerBootId(100);
  c.state_source = StateId(1000);
  c.state_generation = StateGeneration(1);
  c.checkpoint_source = CheckpointId(5000);
  c.checkpoint_generation = CheckpointGeneration(1);
  if (s == RecoveryStrategy::MIGRATE) {
    c.target_worker = WorkerId(2);
    c.target_boot = WorkerBootId(200);
  }
  c.reason_for_existence = std::string(to_string(s)) + " candidate";

  StateEvidence st;
  st.meta.coordinator_epoch = CoordinatorEpoch(1);
  st.meta.observer = WorkerBootId(100);
  st.meta.observed_at = Timestamp(100);
  st.meta.provenance = EvidenceProvenance::MEASURED;
  st.meta.freshness = Freshness::CURRENT;
  st.meta.confidence = Confidence::full();
  st.state_id = StateId(1000);
  st.state_generation = StateGeneration(1);
  st.workload_generation = WorkloadGeneration(1);
  st.checkpoint_generation = CheckpointGeneration(1);
  st.integrity = StateIntegrity::VALID;
  st.size_bytes = ByteCount(1024);
  st.restore_bytes = ByteCount(1024);
  st.preserved_progress = preserved;
  st.durable_progress = preserved;
  st.location = StorageLocation::LOCAL;
  st.is_durable = true;
  st.identity = "workload:v1";
  c.state = st;

  CompatibilityEvidence co;
  co.meta.coordinator_epoch = CoordinatorEpoch(1);
  co.meta.observer = WorkerBootId(100);
  co.meta.observed_at = Timestamp(100);
  co.meta.provenance = EvidenceProvenance::REPORTED;
  co.meta.freshness = Freshness::CURRENT;
  co.compatibility_generation = CompatibilityGeneration(1);
  co.result = feasible ? CompatibilityResult::EXACT : CompatibilityResult::INCOMPATIBLE;
  co.subject_identity = "workload:v1";
  co.state_format = (s == RecoveryStrategy::MIGRATE) ? "worker:2" : "worker:1";
  c.compatibility = co;

  AvailabilityEvidence av;
  av.meta.coordinator_epoch = CoordinatorEpoch(1);
  av.meta.observer = WorkerBootId(100);
  av.meta.observed_at = Timestamp(100);
  av.meta.provenance = EvidenceProvenance::MEASURED;
  av.meta.freshness = Freshness::CURRENT;
  av.worker = (s == RecoveryStrategy::MIGRATE) ? WorkerId(2) : WorkerId(1);
  av.boot = (s == RecoveryStrategy::MIGRATE) ? WorkerBootId(200) : WorkerBootId(100);
  av.engine_incarnation = EngineIncarnationId(1);
  av.is_alive = true;
  av.readiness = feasible ? CandidateReadiness::READY : CandidateReadiness::NOT_READY;
  c.availability = av;

  ResourceEvidence re;
  re.meta.coordinator_epoch = CoordinatorEpoch(1);
  re.meta.observer = WorkerBootId(100);
  re.meta.observed_at = Timestamp(100);
  re.meta.provenance = EvidenceProvenance::MEASURED;
  re.meta.freshness = Freshness::CURRENT;
  re.snapshot = ResourceSnapshotId(1);
  re.generation = ResourceGeneration(1);
  re.resource_kind = "gpu_mem";
  re.required = 100;
  re.available = 10000;
  re.status = ResourceStatus::AVAILABLE;
  c.resource = re;

  TopologyEvidence tp;
  tp.meta.coordinator_epoch = CoordinatorEpoch(1);
  tp.meta.observer = WorkerBootId(100);
  tp.meta.observed_at = Timestamp(100);
  tp.meta.provenance = EvidenceProvenance::MEASURED;
  tp.meta.freshness = Freshness::CURRENT;
  tp.generation = TopologyGeneration(1);
  tp.path_possible = true;
  tp.source = "worker:1";
  tp.destination = "worker:2";
  c.topology = tp;

  TransferEvidence tr;
  tr.meta.coordinator_epoch = CoordinatorEpoch(1);
  tr.meta.observer = WorkerBootId(100);
  tr.meta.observed_at = Timestamp(100);
  tr.meta.provenance = EvidenceProvenance::ESTIMATED;
  tr.meta.freshness = Freshness::CURRENT;
  tr.transfer_bytes = ByteCount(1024);
  tr.bandwidth = Bandwidth(1024 * 1024);
  tr.from = "worker:1";
  tr.to = "worker:2";
  c.transfer = tr;

  ShadowEvidence sh;
  sh.meta.coordinator_epoch = CoordinatorEpoch(1);
  sh.meta.observer = WorkerBootId(100);
  sh.meta.observed_at = Timestamp(100);
  sh.meta.provenance = EvidenceProvenance::MEASURED;
  sh.meta.freshness = Freshness::CURRENT;
  sh.shadow_worker = WorkerId(1);
  sh.shadow_boot = WorkerBootId(100);
  sh.shadow_state_generation = StateGeneration(1);
  sh.lag = Duration(25);
  sh.last_sync_point = Timestamp(90);
  sh.readiness = feasible ? CandidateReadiness::READY : CandidateReadiness::NOT_READY;
  sh.compatibility = CompatibilityResult::EXACT;
  c.shadow = sh;

  CostEvidence cost;
  cost.meta.coordinator_epoch = CoordinatorEpoch(1);
  cost.meta.observer = WorkerBootId(100);
  cost.meta.observed_at = Timestamp(100);
  cost.meta.provenance = EvidenceProvenance::ESTIMATED;
  cost.meta.freshness = Freshness::CURRENT;
  cost.expected_time = time;
  cost.expected_lost_progress = lost;
  cost.expected_preserved_progress = preserved;
  if (s == RecoveryStrategy::RESTORE) cost.restore_time = time;
  if (s == RecoveryStrategy::RECOMPUTE) cost.recompute_time = time;
  if (s == RecoveryStrategy::MIGRATE) cost.transfer_time = time;
  if (s == RecoveryStrategy::SHADOW_PROMOTE) cost.restart_time = time;
  cost.bytes_transferred = ByteCount(1024);
  cost.bytes_read = ByteCount(1024);
  cost.bytes_materialized = ByteCount(1024);
  c.cost = cost;

  AuthorityEvidence auth;
  auth.meta.coordinator_epoch = CoordinatorEpoch(1);
  auth.meta.observer = WorkerBootId(100);
  auth.meta.observed_at = Timestamp(100);
  auth.meta.provenance = EvidenceProvenance::MEASURED;
  auth.meta.freshness = Freshness::CURRENT;
  auth.meta.confidence = Confidence::full();
  auth.coordinator_epoch = CoordinatorEpoch(1);
  auth.policy_generation = PolicyGeneration(1);
  auth.coordinator_authority_valid = true;
  auth.policy_authority_valid = true;
  auth.candidate_authority_valid = true;
  auth.execution_authority_valid = true;
  c.authority = auth;
  return c;
}

void print_decision(RecoveryPlanner& planner, const RecoveryRequest& req,
                    std::vector<RecoveryCandidate> cands, const char* title) {
  auto d = planner.plan(req, cands);
  std::printf("=== %s ===\n  outcome=%s\n", title, to_string(d.outcome));
  for (const auto& rc : d.ranked) {
    std::printf("  rank=%2d feasible_rank=%2d strategy=%-14s feasibility=%-22s "
                "time=%s lost=%s preserved=%s\n",
                rc.rank, rc.feasible_rank, to_string(rc.candidate.strategy),
                to_string(rc.feasibility.result),
                rc.economics.expected_time ? std::to_string(rc.economics.expected_time->value()).c_str() : "?",
                rc.economics.expected_lost_progress ? std::to_string(rc.economics.expected_lost_progress->value()).c_str() : "?",
                rc.economics.expected_preserved_progress ? std::to_string(rc.economics.expected_preserved_progress->value()).c_str() : "?");
    if (rc.feasibility.blocked() && !rc.feasibility.violations.empty())
      std::printf("       rejected: %s\n", to_string(rc.feasibility.violations.front().reason));
  }
  if (d.plan) {
    std::printf("  selected=%s candidate=%llu digest=%s\n",
                to_string(d.plan->strategy), (unsigned long long)d.plan->candidate_id.value(),
                d.plan->decision_digest.c_str());
    std::printf("  deadline_outcome=%s slack=%s\n", to_string(d.plan->deadline.outcome),
                d.plan->deadline.slack ? std::to_string(d.plan->deadline.slack->value()).c_str() : "?");
    std::printf("  what_would_change:\n");
    for (const auto& c : d.plan->explanation.what_would_change.conditions)
      std::printf("    if %s -> %s\n", c.first.c_str(), c.second.c_str());
    std::printf("  rejected_alternatives=%zu\n", d.plan->explanation.rejected_alternatives.size());
  }
  // A new plan supersedes any earlier one for this request.
  std::printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("rplanner usage:\n"
                "  --scenario <RESTORE|RECOMPUTE|SHADOW|MIGRATE|REHYDRATE|FAILOVER|RESTART|NOVALID|DEADLINE>\n"
                "  --validate <persistence-file>\n");
    return 2;
  }

  if (std::strcmp(argv[1], "--validate") == 0 && argc >= 3) {
    std::string path = argv[2];
    auto pos = path.find_last_of("\\/");
    std::string dir = pos == std::string::npos ? "." : path.substr(0, pos);
    auto store = make_file_persistence_store(dir);
    auto blob = store->load("planner_state");
    if (!blob) { std::printf("INVALID_OR_CORRUPT\n"); return 1; }
    std::printf("VALID persistence frame (%zu bytes)\n", blob->size());
    return 0;
  }

  if (std::strcmp(argv[1], "--scenario") == 0 && argc >= 3) {
    std::string scenario = argv[2];
    auto clock = std::make_shared<TestClock>(Timestamp(0));
    RecoveryPlanner planner(clock, make_memory_persistence_store());
    planner.register_worker(WorkerId(1), WorkerBootId(100));
    planner.register_worker(WorkerId(2), WorkerBootId(200));
    planner.set_resource("gpu_mem", 10000);
    planner.set_resource("host_mem", 100000);
    planner.set_recompute_available(WorkloadGeneration(1), true);

    RecoveryRequest req;
    req.request_id = RecoveryRequestId(1);
    req.workload = WorkloadId(1);
    req.workload_generation = WorkloadGeneration(1);
    req.execution = ExecutionId(1);
    req.execution_generation = ExecutionGeneration(1);
    req.failure = FailureId(1);
    req.reason = "worker lost";
    req.coordinator_epoch = CoordinatorEpoch(1);
    req.policy_generation = PolicyGeneration(1);
    req.topology_generation = TopologyGeneration(1);
    req.requested_at = Timestamp(0);

    std::vector<RecoveryCandidate> cands;
    // Baseline: all 7 strategies with varied economics.
    cands.push_back(make_candidate(RecoveryStrategy::RESTORE, 11, Duration(3000), ProgressUnits(100), ProgressUnits(0)));
    cands.push_back(make_candidate(RecoveryStrategy::RESTART, 12, Duration(1000), ProgressUnits(0), ProgressUnits(100)));
    cands.push_back(make_candidate(RecoveryStrategy::MIGRATE, 13, Duration(2000), ProgressUnits(95), ProgressUnits(5)));
    cands.push_back(make_candidate(RecoveryStrategy::REHYDRATE, 14, Duration(1500), ProgressUnits(90), ProgressUnits(10)));
    cands.push_back(make_candidate(RecoveryStrategy::SHADOW_PROMOTE, 15, Duration(50), ProgressUnits(100), ProgressUnits(0)));
    cands.push_back(make_candidate(RecoveryStrategy::FAILOVER, 16, Duration(800), ProgressUnits(100), ProgressUnits(0)));
    cands.push_back(make_candidate(RecoveryStrategy::RECOMPUTE, 17, Duration(1200), ProgressUnits(100), ProgressUnits(0)));

    if (scenario != "RECOMPUTE" && scenario != "DEADLINE") {
      // Only the targeted strategy should be the sole feasible path.
      planner.set_recompute_available(WorkloadGeneration(1), false);
    }
    if (scenario == "RECOMPUTE") {
      req.recovery_deadline = Timestamp(2000);
      for (auto& c : cands) if (c.strategy != RecoveryStrategy::RECOMPUTE)
        c.availability->readiness = CandidateReadiness::NOT_READY;
    } else if (scenario == "RESTORE") {
      for (auto& c : cands) if (c.strategy != RecoveryStrategy::RESTORE)
        c.availability->readiness = CandidateReadiness::NOT_READY;
    } else if (scenario == "MIGRATE") {
      for (auto& c : cands) if (c.strategy != RecoveryStrategy::MIGRATE)
        c.availability->readiness = CandidateReadiness::NOT_READY;
    } else if (scenario == "REHYDRATE") {
      for (auto& c : cands) if (c.strategy != RecoveryStrategy::REHYDRATE)
        c.availability->readiness = CandidateReadiness::NOT_READY;
    } else if (scenario == "FAILOVER") {
      for (auto& c : cands) if (c.strategy != RecoveryStrategy::FAILOVER)
        c.availability->readiness = CandidateReadiness::NOT_READY;
    } else if (scenario == "RESTART") {
      for (auto& c : cands) if (c.strategy != RecoveryStrategy::RESTART)
        c.availability->readiness = CandidateReadiness::NOT_READY;
    } else if (scenario == "SHADOW") {
      for (auto& c : cands) if (c.strategy != RecoveryStrategy::SHADOW_PROMOTE)
        c.availability->readiness = CandidateReadiness::NOT_READY;
    } else if (scenario == "NOVALID") {
      for (auto& c : cands) { c.authority->coordinator_epoch = CoordinatorEpoch(999); c.authority->meta.coordinator_epoch = CoordinatorEpoch(999); }
    } else if (scenario == "DEADLINE") {
      req.recovery_deadline = Timestamp(600);
    }

    print_decision(planner, req, cands, scenario.c_str());
    return 0;
  }

  std::printf("unknown arguments\n");
  return 2;
}
