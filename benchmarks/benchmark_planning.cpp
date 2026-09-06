#include "recovery_planner/recovery_planner.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace recovery_planner;

namespace {

RecoveryCandidate make_candidate(std::uint64_t id, std::uint64_t seed) {
  RecoveryCandidate c;
  c.candidate_id = CandidateId(id);
  c.generation = CandidateGeneration(id);
  c.strategy = static_cast<RecoveryStrategy>((id + seed) % 7);
  c.target_worker = WorkerId(1); c.target_boot = WorkerBootId(100);
  c.target_engine = EngineId(1); c.target_engine_incarnation = EngineIncarnationId(1);
  c.state_source = StateId(1000); c.state_generation = StateGeneration(1);
  c.checkpoint_source = CheckpointId(5000); c.checkpoint_generation = CheckpointGeneration(1);
  c.reason_for_existence = "bench";

  StateEvidence st;
  st.meta.coordinator_epoch = CoordinatorEpoch(1); st.meta.observer = WorkerBootId(100);
  st.meta.observed_at = Timestamp(100); st.meta.provenance = EvidenceProvenance::MEASURED;
  st.meta.freshness = Freshness::CURRENT; st.meta.confidence = Confidence::full();
  st.state_id = StateId(1000); st.state_generation = StateGeneration(1);
  st.workload_generation = WorkloadGeneration(1); st.checkpoint_generation = CheckpointGeneration(1);
  st.integrity = StateIntegrity::VALID; st.size_bytes = ByteCount(1024); st.restore_bytes = ByteCount(1024);
  st.preserved_progress = ProgressUnits(90 + (seed % 10));
  st.durable_progress = st.preserved_progress;
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
  av.engine_incarnation = EngineIncarnationId(1); av.is_alive = true;
  av.readiness = (seed % 7 == 3) ? CandidateReadiness::NOT_READY : CandidateReadiness::READY;
  c.availability = av;

  ResourceEvidence re;
  re.meta.coordinator_epoch = CoordinatorEpoch(1); re.meta.observer = WorkerBootId(100);
  re.meta.observed_at = Timestamp(100); re.meta.provenance = EvidenceProvenance::MEASURED;
  re.meta.freshness = Freshness::CURRENT; re.snapshot = ResourceSnapshotId(1);
  re.generation = ResourceGeneration(1); re.resource_kind = "gpu_mem"; re.required = 100;
  re.available = 10000; re.status = ResourceStatus::AVAILABLE;
  c.resource = re;

  CostEvidence cost;
  cost.meta.coordinator_epoch = CoordinatorEpoch(1); cost.meta.observer = WorkerBootId(100);
  cost.meta.observed_at = Timestamp(100); cost.meta.provenance = EvidenceProvenance::ESTIMATED;
  cost.meta.freshness = Freshness::CURRENT;
  cost.expected_time = Duration((std::int64_t)(1000 + (seed * 7919) % 50000));
  cost.expected_lost_progress = ProgressUnits(seed % 40);
  cost.expected_preserved_progress = st.preserved_progress;
  cost.bytes_transferred = ByteCount(1024); cost.bytes_read = ByteCount(1024);
  c.cost = cost;

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

double ms_since(std::chrono::steady_clock::time_point t) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

RecoveryRequest bench_request() {
  RecoveryRequest r; r.request_id = RecoveryRequestId(1); r.workload = WorkloadId(1);
  r.workload_generation = WorkloadGeneration(1); r.execution = ExecutionId(1);
  r.execution_generation = ExecutionGeneration(1); r.failure = FailureId(1); r.reason = "bench";
  r.coordinator_epoch = CoordinatorEpoch(1); r.policy_generation = PolicyGeneration(1);
  r.topology_generation = TopologyGeneration(1); r.requested_at = Timestamp(0);
  return r;
}

}  // namespace

int main() {
  std::vector<std::size_t> scales = {8, 64, 256, 1000, 10000};
  for (std::size_t n : scales) {
    std::vector<RecoveryCandidate> cands;
    for (std::size_t i = 0; i < n; ++i) cands.push_back(make_candidate(i + 1, i));

    auto clock = std::make_shared<TestClock>(Timestamp(0));
    RecoveryPlanner planner(clock, make_memory_persistence_store());
    planner.register_worker(WorkerId(1), WorkerBootId(100));
    planner.set_resource("gpu_mem", 10000);
    planner.set_recompute_available(WorkloadGeneration(1), true);
    RecoveryRequest req = bench_request();

    // Warm up.
    for (int i = 0; i < 3; ++i) planner.plan(req, cands);

    const int reps = (n < 1000) ? 20 : 3;
    auto t0 = std::chrono::steady_clock::now();
    std::uint64_t outcome_count = 0;
    for (int rep = 0; rep < reps; ++rep) {
      auto d = planner.plan(req, cands);
      outcome_count += (d.outcome == PlanOutcome::PLAN_FOUND || d.outcome == PlanOutcome::NO_VALID_PLAN) ? 1 : 0;
    }
    double total = ms_since(t0);
    double per_call = total / reps;
    double throughput = (double)reps * 1000.0 / (total > 0 ? total : 1.0);
    std::printf("planning N=%-6zu reps=%2d avg=%-8.3f ms throughput=%-8.0f calls/s performed_work=%llu\n",
                n, reps, per_call, throughput, (unsigned long long)outcome_count);
  }

  // Concurrent planning.
  {
    const int threads = 4;
    auto clock = std::make_shared<TestClock>(Timestamp(0));
    RecoveryPlanner planner(clock, make_memory_persistence_store());
    planner.register_worker(WorkerId(1), WorkerBootId(100));
    planner.register_worker(WorkerId(2), WorkerBootId(200));
    planner.set_resource("gpu_mem", 10000);
    planner.set_recompute_available(WorkloadGeneration(1), true);
    std::vector<std::thread> ts;
    auto t0 = std::chrono::steady_clock::now();
    for (int t = 0; t < threads; ++t) {
      ts.emplace_back([&planner, t] {
        for (int i = 0; i < 200; ++i) {
          auto r = bench_request(); r.request_id = RecoveryRequestId(1000 + t * 1000 + i);
          auto c = make_candidate(i + 1, i);
          planner.plan(r, {c, make_candidate(i + 2, i + 7)});
        }
      });
    }
    for (auto& th : ts) th.join();
    double total = ms_since(t0);
    std::printf("concurrent_planning threads=%d completed=%d wall=%.3f ms\n",
                threads, threads * 200, total);
  }

  // Persistence save/load.
  {
    auto store = make_memory_persistence_store();
    auto clock = std::make_shared<TestClock>(Timestamp(0));
    RecoveryPlanner planner(clock, store);
    planner.register_worker(WorkerId(1), WorkerBootId(100));
    planner.set_resource("gpu_mem", 10000);
    planner.set_recompute_available(WorkloadGeneration(1), true);
    for (int i = 0; i < 100; ++i) {
      auto r = bench_request(); r.request_id = RecoveryRequestId(2000 + i);
      planner.plan(r, {make_candidate(i + 1, i)});
    }
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) { planner.save_state(); planner.load_state(); }
    double total = ms_since(t0);
    std::printf("persistence save+load x10: %.3f ms (100 plans)\n", total);
  }

  return 0;
}
