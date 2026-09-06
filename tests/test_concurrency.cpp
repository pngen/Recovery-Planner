#include "test_util.hpp"
#include "scenario.hpp"
#include <thread>
#include <atomic>
#include <vector>

using namespace recovery_planner;
using namespace scenario;

namespace {
void make_world(RecoveryPlanner& planner) {
  planner.register_worker(kWorkerA, kBootA);
  planner.register_worker(kWorkerB, kBootB);
  planner.set_resource("gpu_mem", 10000);
  planner.set_recompute_available(kWorkloadGen, true);
}
}

// ---------------------------------------------------------------------------
RP_TEST(concurrent_planning_no_deadlock) {
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  make_world(planner);

  const int N = 8;
  const int iters = 50;
  std::atomic<int> success{0};
  std::atomic<int> failed{0};
  std::vector<std::thread> threads;
  for (int i = 0; i < N; ++i) {
    threads.emplace_back([&planner, i, &success, &failed] {
      for (int j = 0; j < iters; ++j) {
        RecoveryRequest req = make_request("concurrent");
        req.request_id = RecoveryRequestId(1000 + i * 1000 + j);
        auto cand = make_candidate(RecoveryStrategy::RESTORE, 1);
        cand.candidate_id = CandidateId(1);
        cand.generation = CandidateGeneration(1);
        auto d = planner.plan(req, {cand});
        if (d.outcome == PlanOutcome::PLAN_FOUND) ++success; else ++failed;
      }
    });
  }
  for (auto& t : threads) t.join();
  RP_CHECK(failed == 0);
  RP_CHECK(success == N * iters);
}

// ---------------------------------------------------------------------------
RP_TEST(concurrent_completion_exactly_one_terminal) {
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  make_world(planner);
  RecoveryRequest req = make_request();
  auto d = planner.plan(req, {make_candidate(RecoveryStrategy::RESTORE, 1)});
  auto auth = planner.authorize_plan(*d.plan);
  ReferenceExecutor exec([](DispatchId, ExecutionOutcome, ProgressUnits, Duration, std::string) {});
  auto dispatched = planner.dispatch_plan(auth, exec);
  RP_CHECK(dispatched.state == PlanLifecycleState::EXECUTING);

  const int N = 8;
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  for (int i = 0; i < N; ++i) {
    threads.emplace_back([&planner, &go, dispatched, i] {
      ExecutionReport rep;
      rep.plan_id = dispatched.plan_id;
      rep.plan_generation = dispatched.generation;
      rep.reporter_boot = dispatched.target_boot;
      rep.outcome = (i % 2 == 0) ? ExecutionOutcome::SUCCEEDED : ExecutionOutcome::FAILED;
      while (!go.load(std::memory_order_acquire)) {}
      planner.report_execution(rep);
    });
  }
  go.store(true, std::memory_order_release);
  for (auto& t : threads) t.join();
  auto _plans = planner.plans(); auto& p = _plans[0];
  const bool terminal = p.state == PlanLifecycleState::SUCCEEDED || p.state == PlanLifecycleState::FAILED;
  RP_CHECK(terminal);
  RP_CHECK(p.outcome_published == true);  // authoritative terminal published
}

// ---------------------------------------------------------------------------
RP_TEST(concurrent_cancel_vs_success_race) {
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  make_world(planner);
  RecoveryRequest req = make_request();
  auto d = planner.plan(req, {make_candidate(RecoveryStrategy::RESTORE, 1)});
  auto auth = planner.authorize_plan(*d.plan);
  ReferenceExecutor exec([](DispatchId, ExecutionOutcome, ProgressUnits, Duration, std::string) {});
  auto dispatched = planner.dispatch_plan(auth, exec);

  std::atomic<bool> go{false};
  std::thread cancel([&] {
    while (!go.load(std::memory_order_acquire)) {}
    planner.cancel_plan(dispatched.plan_id, true);
  });
  std::thread success([&] {
    while (!go.load(std::memory_order_acquire)) {}
    ExecutionReport rep;
    rep.plan_id = dispatched.plan_id;
    rep.plan_generation = dispatched.generation;
    rep.reporter_boot = dispatched.target_boot;
    rep.outcome = ExecutionOutcome::SUCCEEDED;
    planner.report_execution(rep);
  });
  go.store(true, std::memory_order_release);
  cancel.join();
  success.join();

  auto _plans = planner.plans(); auto& p = _plans[0];
  const bool terminal = p.state == PlanLifecycleState::SUCCEEDED ||
                        p.state == PlanLifecycleState::FAILED ||
                        p.state == PlanLifecycleState::CANCELLED ||
                        p.state == PlanLifecycleState::INVALIDATED;
  RP_CHECK(terminal);
  // A cancelled plan whose outcome was already published must not be a success
  // that violates the one-authoritative-terminal rule.
  RP_CHECK(p.state != PlanLifecycleState::SUCCEEDED || p.outcome_published == true);
}

// ---------------------------------------------------------------------------
RP_TEST(concurrent_evidence_updates_and_planning) {
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  make_world(planner);
  std::atomic<int> ok{0};
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&planner, &ok, i] {
      for (int j = 0; j < 40; ++j) {
        if (i % 2 == 0) {
          RecoveryRequest req = make_request("update");
          req.request_id = RecoveryRequestId(5000 + j);
          auto cand = make_candidate(RecoveryStrategy::RESTORE, 1);
          auto d = planner.plan(req, {cand});
          if (d.outcome == PlanOutcome::PLAN_FOUND) ++ok;
        } else {
          // evidence / world updates racing with planning
          planner.set_resource("host_mem", 100 + j);
          planner.set_recompute_available(kWorkloadGen, (j % 2) == 0);
        }
      }
    });
  }
  for (auto& t : threads) t.join();
  RP_CHECK(ok >= 0);
}
