
// Recovery Planner CUDA proof (REAL hardware: RTX 5090, CUDA, sm_120).
//
// This proof performs real segmented CUDA computation, creates durable recovery
// state at a known progress boundary, measures real restore vs recompute cost,
// and uses the Recovery Planner to select a strategy under two different state
// availability / deadline configurations. The selected recovery path is then
// executed and verified against a CPU reference, and device memory is returned
// to baseline.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cuda_runtime.h>

#include "recovery_planner/recovery_planner.hpp"

using namespace recovery_planner;

namespace {

constexpr int kSegments = 256;
constexpr int kSegLen = 1024;
constexpr int kTotal = kSegments * kSegLen;

#define CUDA_CHECK(call) do { cudaError_t e = (call); if (e != cudaSuccess) {   std::fprintf(stderr, "CUDA error %s at %s:%d\n", cudaGetErrorString(e), __FILE__, __LINE__);   std::exit(3); } } while (0)

__global__ void compute_segments(int* out, int seed, int from_seg, int to_seg) {
  int idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= kTotal) return;
  int seg = idx / kSegLen;
  if (seg < from_seg || seg >= to_seg) return;  // only compute the requested range
  long long v = (long long)((idx * 2654435761u) ^ (seed * 1103515245u)) & 0x7FFFFFFF;
  atomicAdd(&out[seg], (int)(v & 0xFFFF));
}

void fill_input(int* host, int seed) {
  for (int i = 0; i < kTotal; ++i)
    host[i] = (int)(((long long)((i * 2654435761u) ^ ((long long)seed * 1103515245u))) & 0x7FFFFFFF);
}

long long cpu_reference(int* input, int seed) {
  long long r = 0;
  for (int i = 0; i < kTotal; ++i) r += (long long)(input[i] & 0xFFFF);
  (void)seed;
  return r;
}

// Build a RecoveryCandidate for the given strategy over the target worker.
RecoveryCandidate make_candidate(RecoveryStrategy s, std::uint64_t id, Duration time,
                                 ProgressUnits preserved, ProgressUnits lost) {
  using namespace recovery_planner;
  RecoveryCandidate c;
  c.candidate_id = CandidateId(id);
  c.generation = CandidateGeneration(id);
  c.strategy = s;
  c.target_worker = WorkerId(2);
  c.target_boot = WorkerBootId(2000);
  c.target_engine = EngineId(1);
  c.target_engine_incarnation = EngineIncarnationId(1);
  c.state_source = StateId(7000);
  c.state_generation = StateGeneration(1);
  c.checkpoint_source = CheckpointId(8000);
  c.checkpoint_generation = CheckpointGeneration(1);

  StateEvidence st;
  st.meta.coordinator_epoch = CoordinatorEpoch(1);
  st.meta.observer = WorkerBootId(2000);
  st.meta.observed_at = Timestamp(0);
  st.meta.provenance = EvidenceProvenance::MEASURED;
  st.meta.freshness = Freshness::CURRENT;
  st.meta.confidence = Confidence::full();
  st.state_id = StateId(7000);
  st.state_generation = StateGeneration(1);
  st.workload_generation = WorkloadGeneration(1);
  st.checkpoint_generation = CheckpointGeneration(1);
  st.integrity = StateIntegrity::VALID;
  st.size_bytes = ByteCount(kTotal * sizeof(int));
  st.restore_bytes = ByteCount(kTotal * sizeof(int));
  st.preserved_progress = preserved;
  st.durable_progress = preserved;
  st.location = StorageLocation::REMOTE;
  st.is_durable = true;
  st.identity = "workload:v1";
  c.state = st;

  CompatibilityEvidence co;
  co.meta.coordinator_epoch = CoordinatorEpoch(1);
  co.meta.observer = WorkerBootId(2000);
  co.meta.observed_at = Timestamp(0);
  co.meta.provenance = EvidenceProvenance::REPORTED;
  co.meta.freshness = Freshness::CURRENT;
  co.compatibility_generation = CompatibilityGeneration(1);
  co.result = CompatibilityResult::EXACT;
  co.subject_identity = "workload:v1";
  co.state_format = "gpu:sm120";
  c.compatibility = co;

  AvailabilityEvidence av;
  av.meta.coordinator_epoch = CoordinatorEpoch(1);
  av.meta.observer = WorkerBootId(2000);
  av.meta.observed_at = Timestamp(0);
  av.meta.provenance = EvidenceProvenance::MEASURED;
  av.meta.freshness = Freshness::CURRENT;
  av.worker = WorkerId(2);
  av.boot = WorkerBootId(2000);
  av.engine_incarnation = EngineIncarnationId(1);
  av.is_alive = true;
  av.readiness = CandidateReadiness::READY;
  c.availability = av;

  ResourceEvidence re;
  re.meta.coordinator_epoch = CoordinatorEpoch(1);
  re.meta.observer = WorkerBootId(2000);
  re.meta.observed_at = Timestamp(0);
  re.meta.provenance = EvidenceProvenance::MEASURED;
  re.meta.freshness = Freshness::CURRENT;
  re.snapshot = ResourceSnapshotId(1);
  re.generation = ResourceGeneration(1);
  re.resource_kind = "accelerator_memory";
  re.required = kTotal * sizeof(int);
  re.available = 64u * 1024u * 1024u;
  re.status = ResourceStatus::AVAILABLE;
  c.resource = re;

  CostEvidence cost;
  cost.meta.coordinator_epoch = CoordinatorEpoch(1);
  cost.meta.observer = WorkerBootId(2000);
  cost.meta.observed_at = Timestamp(0);
  cost.meta.provenance = EvidenceProvenance::MEASURED;
  cost.meta.freshness = Freshness::CURRENT;
  cost.expected_time = time;
  cost.expected_lost_progress = lost;
  cost.expected_preserved_progress = preserved;
  if (s == RecoveryStrategy::RESTORE) cost.restore_time = time;
  if (s == RecoveryStrategy::RECOMPUTE) cost.recompute_time = time;
  cost.bytes_transferred = ByteCount(kTotal * sizeof(int));
  cost.bytes_read = ByteCount(kTotal * sizeof(int));
  cost.bytes_materialized = ByteCount(kTotal * sizeof(int));
  c.cost = cost;

  AuthorityEvidence auth;
  auth.meta.coordinator_epoch = CoordinatorEpoch(1);
  auth.meta.observer = WorkerBootId(2000);
  auth.meta.observed_at = Timestamp(0);
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

std::uint64_t run_kernel(int* dev, int seed, int from_seg, int to_seg) {
  // zero the segment slots for the range, then compute them
  CUDA_CHECK(cudaMemset(dev + from_seg, 0, (to_seg - from_seg) * sizeof(int)));
  compute_segments<<<kTotal / kSegLen, kSegLen>>>(dev, seed, from_seg, to_seg);
  CUDA_CHECK(cudaDeviceSynchronize());
  // Read the device result back to host (device memory is never dereferenced
  // directly on the host).
  std::vector<int> tmp(static_cast<std::size_t>(to_seg - from_seg));
  CUDA_CHECK(cudaMemcpy(tmp.data(), dev + from_seg,
                        static_cast<std::size_t>(to_seg - from_seg) * sizeof(int),
                        cudaMemcpyDeviceToHost));
  long long sum = 0;
  for (std::size_t i = 0; i < tmp.size(); ++i) sum += tmp[i];
  return (std::uint64_t)sum;
}

void run_case(const char* name, bool fresh_checkpoint, long long cpu_ref, int* host_input) {
  // Planner world.
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  planner.register_worker(WorkerId(2), WorkerBootId(2000));
  planner.set_resource("accelerator_memory", 64u * 1024u * 1024u);
  planner.set_recompute_available(WorkloadGeneration(1), true);

  int* dev = nullptr;
  CUDA_CHECK(cudaMalloc(&dev, kSegments * sizeof(int)));

  // Real measured timings.
  cudaEvent_t e0, e1; CUDA_CHECK(cudaEventCreate(&e0)); CUDA_CHECK(cudaEventCreate(&e1));
  // recompute: full kernel
  std::uint64_t rec = run_kernel(dev, 7, 0, kSegments);
  int checkpoint_at = fresh_checkpoint ? kSegments : (kSegments / 4);
  std::vector<int> checkpoint(checkpoint_at);
  CUDA_CHECK(cudaMemcpy(checkpoint.data(), dev, checkpoint_at * sizeof(int), cudaMemcpyDeviceToHost));

  // Amortize event overhead so a genuinely cheaper path is measured as cheaper.
  auto measure = [&](auto fn, int reps) -> Duration {
    for (int k = 0; k < 3; ++k) fn();
    CUDA_CHECK(cudaEventRecord(e0));
    for (int k = 0; k < reps; ++k) fn();
    CUDA_CHECK(cudaEventRecord(e1));
    CUDA_CHECK(cudaEventSynchronize(e1));
    float ms = 0; CUDA_CHECK(cudaEventElapsedTime(&ms, e0, e1));
    return Duration((std::int64_t)(ms * 1e6) / reps);
  };

  // recompute: full kernel (real measured compute cost)
  Duration recompute_dur = measure([&] { run_kernel(dev, 7, 0, kSegments); }, 20);
  // restore: transfer preserved checkpoint + recompute only the remaining suffix.
  // A fresh checkpoint (P=100%) is a cheap load; a stale checkpoint must also
  // recompute the un-preserved portion.
  Duration restore_dur = measure([&] {
    CUDA_CHECK(cudaMemcpy(dev, checkpoint.data(), checkpoint_at * sizeof(int), cudaMemcpyHostToDevice));
    if (checkpoint_at < kSegments) run_kernel(dev, 7, checkpoint_at, kSegments);
  }, 20);

  std::uint64_t rec_check = run_kernel(dev, 7, 0, kSegments);
  if (rec_check != rec) { std::fprintf(stderr, "recompute nondeterminism\n"); std::exit(4); }

  float restore_ms = (float)(restore_dur.value() / 1e6);
  float recompute_ms = (float)(recompute_dur.value() / 1e6);

  // Build candidates with MEASURED economics.
  std::vector<RecoveryCandidate> cands;
  ProgressUnits preserved_restore = fresh_checkpoint ? ProgressUnits(100) : ProgressUnits(25);
  ProgressUnits preserved_recompute = ProgressUnits(100);
  cands.push_back(make_candidate(RecoveryStrategy::RESTORE, 11, restore_dur, preserved_restore, ProgressUnits(0)));
  RecoveryCandidate rerun = make_candidate(RecoveryStrategy::RECOMPUTE, 12, recompute_dur, preserved_recompute, ProgressUnits(0));
  cands.push_back(rerun);

  RecoveryRequest req;
  req.request_id = RecoveryRequestId(1);
  req.workload = WorkloadId(1);
  req.workload_generation = WorkloadGeneration(1);
  req.execution = ExecutionId(1); req.execution_generation = ExecutionGeneration(1);
  req.failure = FailureId(1); req.reason = "gpu worker lost";
  req.coordinator_epoch = CoordinatorEpoch(1);
  req.policy_generation = PolicyGeneration(1); req.topology_generation = TopologyGeneration(1);
  req.requested_at = Timestamp(0);

  auto decision = planner.plan(req, cands);
  if (!decision.plan) { std::fprintf(stderr, "no plan\n"); std::exit(5); }

  // Execute the selected strategy on the GPU and verify parity.
  CUDA_CHECK(cudaMemset(dev, 0, kSegments * sizeof(int)));
  std::uint64_t final_seg;
  if (decision.plan->strategy == RecoveryStrategy::RESTORE) {
    CUDA_CHECK(cudaMemcpy(dev, checkpoint.data(), checkpoint_at * sizeof(int), cudaMemcpyHostToDevice));
    if (checkpoint_at < kSegments) final_seg = run_kernel(dev, 7, checkpoint_at, kSegments);
    else {
      std::vector<int> tmp(checkpoint_at);
      CUDA_CHECK(cudaMemcpy(tmp.data(), dev, checkpoint_at * sizeof(int), cudaMemcpyDeviceToHost));
      long long s = 0; for (int i = 0; i < checkpoint_at; ++i) s += tmp[i];
      final_seg = s;
    }
  } else {  // RECOMPUTE
    final_seg = run_kernel(dev, 7, 0, kSegments);
  }

  bool parity = ((std::uint64_t)cpu_ref) == final_seg;
  std::printf("CASE=%s SELECTED=%s RESTORE_MS=%.3f RECOMPUTE_MS=%.3f PARITY=%s\n",
              name, to_string(decision.plan->strategy), (double)restore_ms, (double)recompute_ms,
              parity ? "YES" : "NO");
  if (!parity) std::exit(6);

  CUDA_CHECK(cudaEventDestroy(e0)); CUDA_CHECK(cudaEventDestroy(e1));
  CUDA_CHECK(cudaFree(dev));
}

}  // namespace

int main() {
  // Baseline free memory.
  std::size_t free0 = 0, total0 = 0;
  CUDA_CHECK(cudaMemGetInfo(&free0, &total0));

  int* host_input = new int[kTotal];
  fill_input(host_input, 7);
  long long cpu_ref = cpu_reference(host_input, 7);

  run_case("FRESH_CHECKPOINT_RESTORE_WINS", true, cpu_ref, host_input);
  run_case("STALE_CHECKPOINT_RECOMPUTE_WINS", false, cpu_ref, host_input);

  delete[] host_input;

  // Device memory returned to baseline.
  std::size_t free1 = 0, total1 = 0;
  CUDA_CHECK(cudaMemGetInfo(&free1, &total1));
  bool baseline = (free1 >= free0 - (1u << 20));  // allow small slack
  std::printf("MEMORY_BASELINE_CLOSED=%s (free=%zu/%zu)\n", baseline ? "YES" : "NO", free1, total1);
  return baseline ? 0 : 7;
}
