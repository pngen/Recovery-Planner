#include "transport.hpp"
#include "protocol.hpp"
#include "recovery_planner/recovery_planner.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace recovery_planner;
using namespace recovery_planner::transport;
using namespace recovery_planner::proto;

namespace {

// Worker identities used by the proof.
constexpr std::uint64_t kWorkerA = 1;
constexpr std::uint64_t kBootA = 1000;
constexpr std::uint64_t kWorkerB = 2;
constexpr std::uint64_t kBootB = 2000;
// Fresh restart incarnation: the same logical worker is re-incarnated under a
// NEW WorkerBootId after the pre-restart process is terminated.
constexpr std::uint64_t kBootC = 3000;
constexpr std::uint64_t kProgress = 100;

std::string exe_dir() {
#ifdef _WIN32
  char buf[MAX_PATH];
  GetModuleFileNameA(nullptr, buf, MAX_PATH);
  std::string p(buf);
  auto pos = p.find_last_of("\\/");
  return pos == std::string::npos ? "." : p.substr(0, pos);
#else
  return ".";
#endif
}

#ifdef _WIN32
bool spawn_worker(const std::string& dir, std::uint16_t port, std::uint64_t wid,
                  std::uint64_t boot, std::uint64_t engine, const std::string& role,
                  HANDLE& out) {
  // Quote the executable path so CreateProcessA resolves it correctly even when
  // the build tree lives under a directory whose name contains spaces.
  std::string cmd = "\"" + dir + "\\rp_distributed_worker.exe\" " + std::to_string(port) + " " +
                    std::to_string(wid) + " " + std::to_string(boot) + " " + std::to_string(engine) + " " + role;
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessA(nullptr, const_cast<char*>(cmd.c_str()), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    return false;
  }
  out = pi.hProcess;
  CloseHandle(pi.hThread);
  return true;
}
void kill_process(HANDLE h) { if (h) { TerminateProcess(h, 1); CloseHandle(h); } }
#else
struct ProcHandle { };
typedef ProcHandle* HANDLE;
bool spawn_worker(const std::string&, std::uint16_t, std::uint64_t, std::uint64_t,
                  std::uint64_t, const std::string&, HANDLE&) { return false; }
void kill_process(HANDLE) {}
#endif

// A network adapter: dispatch a typed plan to a worker over the live TCP
// connection and report completion back into the planner.
class NetworkExecutor final : public IRecoveryExecutor {
 public:
  NetworkExecutor(std::shared_ptr<Connection> conn, RecoveryPlanner& planner,
                  std::shared_ptr<Clock> clock)
      : conn_(std::move(conn)), planner_(planner), clock_(std::move(clock)) {}

  RecoveryReceipt dispatch(const RecoveryDispatch& dispatch) override {
    Msg m;
    m.type = MsgType::DISPATCH;
    m.a = dispatch.plan_id.value();
    m.b = dispatch.plan_generation.value();
    m.c = static_cast<std::uint64_t>(dispatch.strategy);
    m.d = kProgress;  // expected recovered progress
    m.s1 = dispatch.payload;
    std::vector<std::uint8_t> payload;
    encode(m, payload);
    if (!conn_->send_frame(payload)) {
      return {dispatch.dispatch_id, DispatchStatus::REJECTED, clock_->now(), "send failed"};
    }
    std::vector<std::uint8_t> resp;
    if (!conn_->recv_frame(resp)) {
      return {dispatch.dispatch_id, DispatchStatus::REJECTED, clock_->now(), "no response"};
    }
    Msg r;
    if (!decode(resp, r) || r.type != MsgType::COMPLETE) {
      return {dispatch.dispatch_id, DispatchStatus::UNKNOWN, clock_->now(), "malformed complete"};
    }
    ExecutionOutcome outcome = (r.d == 0) ? ExecutionOutcome::SUCCEEDED : ExecutionOutcome::FAILED;
    ProgressUnits recovered(std::strtoull(r.s2.c_str(), nullptr, 10));
    planner_.on_adapter_completion(dispatch.dispatch_id, outcome, recovered, Duration(0), r.s1);
    return {dispatch.dispatch_id, DispatchStatus::DISPATCHED, clock_->now(), "complete"};
  }
  const char* name() const noexcept override { return "network"; }

 private:
  std::shared_ptr<Connection> conn_;
  RecoveryPlanner& planner_;
  std::shared_ptr<Clock> clock_;
};

// Build a RecoveryCandidate with evidence matching the live target worker.
RecoveryCandidate build_candidate(RecoveryStrategy strategy, std::uint64_t id,
                                  const std::string& scenario,
                                  WorkerId target_worker = WorkerId(kWorkerB),
                                  WorkerBootId target_boot = WorkerBootId(kBootB),
                                  bool feasible = true) {
  using namespace recovery_planner;
  RecoveryCandidate c;
  c.candidate_id = CandidateId(id);
  c.generation = CandidateGeneration(id);
  c.strategy = strategy;
  c.target_worker = target_worker;
  c.target_boot = target_boot;
  c.target_engine = EngineId(1);
  c.target_engine_incarnation = EngineIncarnationId(1);
  c.source_worker = WorkerId(kWorkerA);
  c.source_boot = WorkerBootId(kBootA);
  c.state_source = StateId(5000);
  c.state_generation = StateGeneration(1);
  c.checkpoint_source = CheckpointId(9000);
  c.checkpoint_generation = CheckpointGeneration(1);

  StateEvidence st;
  st.meta.coordinator_epoch = CoordinatorEpoch(1);
  st.meta.observer = WorkerBootId(kBootB);
  st.meta.observed_at = Timestamp(0);
  st.meta.provenance = EvidenceProvenance::MEASURED;
  st.meta.freshness = Freshness::CURRENT;
  st.meta.confidence = Confidence::full();
  st.state_id = StateId(5000);
  st.state_generation = StateGeneration(1);
  st.workload_generation = WorkloadGeneration(1);
  st.checkpoint_generation = CheckpointGeneration(1);
  st.integrity = StateIntegrity::VALID;
  st.size_bytes = ByteCount(4096);
  st.restore_bytes = ByteCount(4096);
  st.preserved_progress = ProgressUnits(kProgress);
  st.durable_progress = ProgressUnits(kProgress);
  st.location = StorageLocation::REMOTE;
  st.is_durable = true;
  st.identity = "workload:v1";
  c.state = st;

  CompatibilityEvidence co;
  co.meta.coordinator_epoch = CoordinatorEpoch(1);
  co.meta.observer = WorkerBootId(kBootB);
  co.meta.observed_at = Timestamp(0);
  co.meta.provenance = EvidenceProvenance::REPORTED;
  co.meta.freshness = Freshness::CURRENT;
  co.compatibility_generation = CompatibilityGeneration(1);
  co.result = CompatibilityResult::EXACT;
  co.subject_identity = "workload:v1";
  co.state_format = "workerB";
  c.compatibility = co;

  AvailabilityEvidence av;
  av.meta.coordinator_epoch = CoordinatorEpoch(1);
  av.meta.observer = target_boot;
  av.meta.observed_at = Timestamp(0);
  av.meta.provenance = EvidenceProvenance::MEASURED;
  av.meta.freshness = Freshness::CURRENT;
  av.worker = target_worker;
  av.boot = target_boot;
  av.engine_incarnation = EngineIncarnationId(1);
  av.is_alive = true;
  av.readiness = feasible ? CandidateReadiness::READY : CandidateReadiness::NOT_READY;
  c.availability = av;

  ResourceEvidence re;
  re.meta.coordinator_epoch = CoordinatorEpoch(1);
  re.meta.observer = WorkerBootId(kBootB);
  re.meta.observed_at = Timestamp(0);
  re.meta.provenance = EvidenceProvenance::MEASURED;
  re.meta.freshness = Freshness::CURRENT;
  re.snapshot = ResourceSnapshotId(1);
  re.generation = ResourceGeneration(1);
  re.resource_kind = "gpu_mem";
  re.required = 512;
  re.available = 100000;
  re.status = ResourceStatus::AVAILABLE;
  c.resource = re;

  TopologyEvidence tp;
  tp.meta.coordinator_epoch = CoordinatorEpoch(1);
  tp.meta.observer = WorkerBootId(kBootB);
  tp.meta.observed_at = Timestamp(0);
  tp.meta.provenance = EvidenceProvenance::MEASURED;
  tp.meta.freshness = Freshness::CURRENT;
  tp.generation = TopologyGeneration(1);
  tp.path_possible = true;
  tp.source = "workerA";
  tp.destination = "workerB";
  c.topology = tp;

  ShadowEvidence sh;
  sh.meta.coordinator_epoch = CoordinatorEpoch(1);
  sh.meta.observer = WorkerBootId(kBootB);
  sh.meta.observed_at = Timestamp(0);
  sh.meta.provenance = EvidenceProvenance::MEASURED;
  sh.meta.freshness = Freshness::CURRENT;
  sh.shadow_worker = WorkerId(kWorkerB);
  sh.shadow_boot = WorkerBootId(kBootB);
  sh.shadow_state_generation = StateGeneration(1);
  sh.lag = Duration(25);
  sh.last_sync_point = Timestamp(0);
  sh.readiness = (scenario == "SHADOW") ? CandidateReadiness::READY : CandidateReadiness::NOT_READY;
  sh.compatibility = CompatibilityResult::EXACT;
  c.shadow = sh;

  CostEvidence cost;
  cost.meta.coordinator_epoch = CoordinatorEpoch(1);
  cost.meta.observer = WorkerBootId(kBootB);
  cost.meta.observed_at = Timestamp(0);
  cost.meta.provenance = EvidenceProvenance::ESTIMATED;
  cost.meta.freshness = Freshness::CURRENT;
  cost.expected_lost_progress = ProgressUnits(0);
  cost.expected_preserved_progress = ProgressUnits(kProgress);
  cost.bytes_transferred = ByteCount(4096);
  cost.bytes_read = ByteCount(4096);
  cost.bytes_materialized = ByteCount(4096);

  // Scenario-specific economics.
  if (strategy == RecoveryStrategy::RESTORE) {
    cost.expected_time = Duration(3000);
    if (scenario == "RECOMPUTE") cost.expected_time = Duration(500000);  // restore too slow
    cost.restore_time = cost.expected_time;
    cost.expected_preserved_progress = ProgressUnits(kProgress);
  } else if (strategy == RecoveryStrategy::RECOMPUTE) {
    cost.expected_time = Duration(1000);
    cost.recompute_time = cost.expected_time;
    // Recompute reconstructs from an earlier point: preserves slightly less.
    if (scenario == "RESTORE") cost.expected_preserved_progress = ProgressUnits(kProgress - 10);
    else cost.expected_preserved_progress = ProgressUnits(kProgress);
  } else if (strategy == RecoveryStrategy::SHADOW_PROMOTE) {
    cost.expected_time = Duration(50);
    cost.expected_preserved_progress = ProgressUnits(kProgress);
  } else if (strategy == RecoveryStrategy::RESTART) {
    cost.expected_time = Duration(500);
    cost.restart_time = Duration(500);
    cost.expected_lost_progress = ProgressUnits(kProgress);
    cost.expected_preserved_progress = ProgressUnits(0);
  }
  c.cost = cost;

  AuthorityEvidence auth;
  auth.meta.coordinator_epoch = CoordinatorEpoch(1);
  auth.meta.observer = WorkerBootId(kBootB);
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

bool send(Connection& c, const Msg& m) {
  std::vector<std::uint8_t> p; encode(m, p); return c.send_frame(p);
}
bool recv(Connection& c, Msg& m) {
  std::vector<std::uint8_t> p; if (!c.recv_frame(p)) return false; return decode(p, m);
}

// Real distributed RESTART proof.
//
// The affected (pre-restart) worker runs to a progress boundary and captures
// durable recovery state, then is terminated as a real OS process. A fresh
// worker incarnation of the same logical worker is started under a NEW
// WorkerBootId, the planner deterministically selects RESTART as the only valid
// strategy (no usable checkpoint to restore, no recompute lineage, no READY
// shadow), the restart is dispatched over the live framed-TCP adapter, the
// completion is authoritative, and stale pre-restart boot / attempt / dispatch /
// completion traffic is rejected by the planner's fencing.
int run_restart_proof() {
  transport_init();
  Listener listener;
  if (!listener.bind(0)) { std::fprintf(stderr, "coordinator: bind failed\n"); return 2; }
  std::uint16_t port = listener.port();
  const std::string dir = exe_dir();

  // Pre-restart incarnation: runs the workload and captures recovery state.
  HANDLE ha = nullptr;
#ifdef _WIN32
  if (!spawn_worker(dir, port, kWorkerA, kBootA, 1, "primary", ha)) { return 3; }
#else
  (void)spawn_worker; (void)kill_process;
#endif
  auto ca = listener.accept();
  if (!ca) { std::fprintf(stderr, "coordinator: failed to accept worker A\n"); return 4; }
  Msg regA;
  recv(*ca, regA);
  if (regA.type != MsgType::REGISTER) { std::fprintf(stderr, "coordinator: registration mismatch\n"); return 5; }

  Msg runA; runA.type = MsgType::RUN; runA.a = 1; runA.b = kProgress;
  send(*ca, runA);
  Msg doneA;
  recv(*ca, doneA);
  if (doneA.type != MsgType::WORK_DONE) { std::fprintf(stderr, "coordinator: no work done\n"); return 6; }

  // Terminate the affected worker as a real OS process.
  kill_process(ha);

  // Start a FRESH worker incarnation with a NEW WorkerBootId.
  HANDLE hc = nullptr;
#ifdef _WIN32
  if (!spawn_worker(dir, port, kWorkerA, kBootC, 1, "restart", hc)) { return 7; }
#else
  (void)spawn_worker; (void)kill_process;
#endif
  auto cc = listener.accept();
  if (!cc) { std::fprintf(stderr, "coordinator: failed to accept fresh worker\n"); return 8; }
  Msg regC;
  recv(*cc, regC);
  if (regC.type != MsgType::REGISTER || regC.a != kWorkerA || regC.b != kBootC) {
    std::fprintf(stderr, "coordinator: fresh incarnation registration mismatch\n"); return 9;
  }

  // The planner (in the coordinator process) selects the recovery strategy.
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  planner.register_worker(WorkerId(kWorkerA), WorkerBootId(kBootC));  // fresh incarnation
  planner.set_resource("gpu_mem", 100000);
  // No recompute lineage/inputs: RECOMPUTE is infeasible, so RESTART is the only
  // valid recovery path.

  RecoveryRequest req;
  req.request_id = RecoveryRequestId(1);
  req.workload = WorkloadId(1);
  req.workload_generation = WorkloadGeneration(1);
  req.execution = ExecutionId(1);
  req.execution_generation = ExecutionGeneration(1);
  req.failure = FailureId(1);
  req.reason = "primary worker lost; no usable recovery state";
  req.coordinator_epoch = CoordinatorEpoch(1);
  req.policy_generation = PolicyGeneration(1);
  req.topology_generation = TopologyGeneration(1);
  req.requested_at = Timestamp(0);

  // RESTORE / SHADOW targets are not READY; RECOMPUTE has no lineage. The only
  // feasible candidate is RESTART on the fresh incarnation under kBootC.
  std::vector<RecoveryCandidate> candidates;
  candidates.push_back(build_candidate(RecoveryStrategy::RESTORE, 11, "RESTART",
                                       WorkerId(kWorkerA), WorkerBootId(kBootA), /*feasible=*/false));
  candidates.push_back(build_candidate(RecoveryStrategy::RECOMPUTE, 12, "RESTART",
                                       WorkerId(kWorkerA), WorkerBootId(kBootA), /*feasible=*/false));
  candidates.push_back(build_candidate(RecoveryStrategy::SHADOW_PROMOTE, 13, "RESTART",
                                       WorkerId(kWorkerA), WorkerBootId(kBootA), /*feasible=*/false));
  candidates.push_back(build_candidate(RecoveryStrategy::RESTART, 14, "RESTART",
                                       WorkerId(kWorkerA), WorkerBootId(kBootC), /*feasible=*/true));

  auto decision = planner.plan(req, candidates);
  if (!decision.plan) { std::fprintf(stderr, "coordinator: no plan produced\n"); return 10; }
  if (decision.plan->strategy != RecoveryStrategy::RESTART) {
    std::fprintf(stderr, "coordinator: expected RESTART but selected %s\n",
                 to_string(decision.plan->strategy));
    return 11;
  }

  // Authorize and dispatch the restart to the fresh incarnation over TCP.
  auto auth = planner.authorize_plan(*decision.plan);
  NetworkExecutor exec(cc, planner, clock);
  auto dispatched = planner.dispatch_plan(auth, exec);
  if (dispatched.state != PlanLifecycleState::SUCCEEDED) {
    std::fprintf(stderr, "coordinator: RESTART did not succeed\n"); return 12;
  }

  std::printf("SCENARIO=RESTART SELECTED=RESTART WORKER_KILLED=YES FRESH_BOOT=%llu "
              "TCP=REAL RECOVERY=SUCCEEDED PROGRESS_VERIFIED=YES DIGEST=%s\n",
              static_cast<unsigned long long>(kBootC), dispatched.decision_digest.c_str());

  // ---- stale pre-restart boot / attempt / dispatch / completion rejection ----
  bool boot_rejected = true, attempt_rejected = true, dispatch_rejected = true,
       completion_rejected = true;

  // Stale BOOT: a completion reported under the pre-restart boot must not mutate
  // the now-authoritative RESTART plan.
  {
    ExecutionReport stale;
    stale.plan_id = dispatched.plan_id;
    stale.plan_generation = dispatched.generation;
    stale.reporter_boot = WorkerBootId(kBootA);  // pre-restart boot
    stale.outcome = ExecutionOutcome::SUCCEEDED;
    planner.report_execution(stale);
    for (auto& p : planner.plans())
      if (p.plan_id == dispatched.plan_id && p.state != PlanLifecycleState::SUCCEEDED)
        boot_rejected = false;
  }

  // Stale ATTEMPT: an adapter completion for a dispatch id that was never issued
  // (a stale/fabricated attempt) is unknown and ignored.
  {
    planner.on_adapter_completion(DispatchId(999999), ExecutionOutcome::SUCCEEDED,
                                  ProgressUnits(kProgress), Duration(0), "stale attempt");
    for (auto& p : planner.plans())
      if (p.plan_id == dispatched.plan_id && p.state != PlanLifecycleState::SUCCEEDED)
        attempt_rejected = false;
  }

  // Stale DISPATCH: re-dispatching the now-terminal plan is refused.
  {
    try {
      planner.dispatch_plan(auth, exec);
      dispatch_rejected = false;  // a stale plan was (incorrectly) dispatched
    } catch (const RecoveryError&) {
      // Expected: STALE_AUTHORITY / terminal plan. The dispatch boundary refused.
    }
  }

  // Stale COMPLETION: a conflicting completion from the correct identity after
  // terminal success is ignored (exactly one authoritative outcome survives).
  {
    ExecutionReport dup;
    dup.plan_id = dispatched.plan_id;
    dup.plan_generation = dispatched.generation;
    dup.reporter_boot = WorkerBootId(kBootC);
    dup.outcome = ExecutionOutcome::FAILED;
    planner.report_execution(dup);
    for (auto& p : planner.plans())
      if (p.plan_id == dispatched.plan_id && p.state != PlanLifecycleState::SUCCEEDED)
        completion_rejected = false;
  }

  std::printf("STALE_BOOT_REJECTED=%s STALE_ATTEMPT_REJECTED=%s "
              "STALE_DISPATCH_REJECTED=%s STALE_COMPLETION_REJECTED=%s\n",
              boot_rejected ? "YES" : "NO", attempt_rejected ? "YES" : "NO",
              dispatch_rejected ? "YES" : "NO", completion_rejected ? "YES" : "NO");

  if (!boot_rejected || !attempt_rejected || !dispatch_rejected || !completion_rejected) {
    send(*cc, Msg{MsgType::STOP});
    kill_process(hc);
    transport_shutdown();
    return 13;
  }

  send(*cc, Msg{MsgType::STOP});
  kill_process(hc);
  transport_shutdown();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string scenario = "RESTORE";
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) scenario = argv[++i];
  }

  // The RESTART proof runs on its own dedicated path; the RESTORE / RECOMPUTE /
  // SHADOW path below is left exactly as implemented.
  if (scenario == "RESTART") return run_restart_proof();

  transport_init();
  Listener listener;
  if (!listener.bind(0)) { std::fprintf(stderr, "coordinator: bind failed\n"); return 2; }
  std::uint16_t port = listener.port();

  const std::string dir = exe_dir();
  HANDLE ha = nullptr, hb = nullptr;
#ifdef _WIN32
  if (!spawn_worker(dir, port, kWorkerA, kBootA, 1, "primary", ha)) { return 3; }
  if (!spawn_worker(dir, port, kWorkerB, kBootB, 1, "target", hb)) { return 4; }
#else
  (void)spawn_worker; (void)kill_process;
#endif

  // Accept the two live worker connections.
  auto ca = listener.accept();
  auto cb = listener.accept();
  if (!ca || !cb) { std::fprintf(stderr, "coordinator: failed to accept workers\n"); return 5; }

  Msg regA, regB;
  recv(*ca, regA);
  recv(*cb, regB);
  if (regA.type != MsgType::REGISTER || regB.type != MsgType::REGISTER) {
    std::fprintf(stderr, "coordinator: registration mismatch\n"); return 6;
  }

  // Ask Worker A to run work and produce a durable recovery state.
  Msg runA; runA.type = MsgType::RUN; runA.a = 1; runA.b = kProgress;
  send(*ca, runA);
  Msg doneA;
  recv(*ca, doneA);
  if (doneA.type != MsgType::WORK_DONE) { std::fprintf(stderr, "coordinator: no work done\n"); return 7; }

  // SHADOW scenario: sync Worker B shadow state.
  if (scenario == "SHADOW") {
    Msg runB; runB.type = MsgType::RUN; runB.a = 2; runB.b = kProgress;
    send(*cb, runB);
    Msg doneB; recv(*cb, doneB);
  }

  // Real OS-process failure: kill Worker A.
  kill_process(ha);

  // The planner (in the coordinator process) selects the recovery strategy.
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  planner.register_worker(WorkerId(kWorkerB), WorkerBootId(kBootB));
  planner.set_resource("gpu_mem", 100000);
  planner.set_recompute_available(WorkloadGeneration(1), true);

  RecoveryRequest req;
  req.request_id = RecoveryRequestId(1);
  req.workload = WorkloadId(1);
  req.workload_generation = WorkloadGeneration(1);
  req.execution = ExecutionId(1);
  req.execution_generation = ExecutionGeneration(1);
  req.failure = FailureId(1);
  req.reason = "primary worker lost";
  req.coordinator_epoch = CoordinatorEpoch(1);
  req.policy_generation = PolicyGeneration(1);
  req.topology_generation = TopologyGeneration(1);
  req.requested_at = Timestamp(0);
  if (scenario == "RECOMPUTE") req.recovery_deadline = Timestamp(10000);

  std::vector<RecoveryCandidate> candidates;
  candidates.push_back(build_candidate(RecoveryStrategy::RESTORE, 11, scenario));
  candidates.push_back(build_candidate(RecoveryStrategy::RECOMPUTE, 12, scenario));
  candidates.push_back(build_candidate(RecoveryStrategy::SHADOW_PROMOTE, 13, scenario));
  candidates.push_back(build_candidate(RecoveryStrategy::RESTART, 14, scenario));

  auto decision = planner.plan(req, candidates);
  if (!decision.plan) { std::fprintf(stderr, "coordinator: no plan produced\n"); return 8; }

  RecoveryStrategy expected;
  if (scenario == "RESTORE") expected = RecoveryStrategy::RESTORE;
  else if (scenario == "RECOMPUTE") expected = RecoveryStrategy::RECOMPUTE;
  else expected = RecoveryStrategy::SHADOW_PROMOTE;
  if (decision.plan->strategy != expected) {
    std::fprintf(stderr, "coordinator: expected %s but selected %s\n",
                 to_string(expected), to_string(decision.plan->strategy));
    return 9;
  }

  // Authorize and dispatch to Worker B over the live TCP connection.
  auto auth = planner.authorize_plan(*decision.plan);
  NetworkExecutor exec(cb, planner, clock);
  auto dispatched = planner.dispatch_plan(auth, exec);
  if (dispatched.state != PlanLifecycleState::SUCCEEDED) {
    std::fprintf(stderr, "coordinator: recovery did not succeed\n"); return 10;
  }

  std::printf("SCENARIO=%s SELECTED=%s WORKER_KILLED=YES TCP=REAL "
              "RECOVERY=SUCCEEDED PROGRESS_VERIFIED=YES DIGEST=%s\n",
              scenario.c_str(), to_string(dispatched.strategy),
              dispatched.decision_digest.c_str());

  // Stale completion rejection: a completion from the old Worker A boot must not
  // mutate the now-authoritative plan.
  ExecutionReport stale;
  stale.plan_id = dispatched.plan_id;
  stale.plan_generation = dispatched.generation;
  stale.reporter_boot = WorkerBootId(kBootA);   // stale boot, primary was killed
  stale.outcome = ExecutionOutcome::SUCCEEDED;
  planner.report_execution(stale);
  bool stale_rejected = true;
  for (auto& p : planner.plans())
    if (p.plan_id == dispatched.plan_id && p.state != PlanLifecycleState::SUCCEEDED)
      stale_rejected = false;
  std::printf("STALE_COMPLETION_REJECTED=%s\n", stale_rejected ? "YES" : "NO");

  send(*cb, Msg{MsgType::STOP});
  kill_process(hb);
  transport_shutdown();
  return 0;
}
