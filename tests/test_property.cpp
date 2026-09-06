#include "test_util.hpp"
#include "scenario.hpp"
#include <functional>
#include <map>

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
RP_TEST(id_ordering_hash_stream_deterministic) {
  CoordinatorEpoch a(5), b(10);
  RP_CHECK(a < b);
  RP_CHECK(a != b);
  std::map<CoordinatorEpoch, int> m;
  m[a] = 1; m[b] = 2;
  RP_CHECK(m.begin()->first == a);
  const auto did = WorkerBootId(42);
  RP_CHECK(did.value() == 42);
  (void)std::hash<WorkerBootId>{}(did);
  (void)std::hash<Duration>{}(Duration(1));
}

// ---------------------------------------------------------------------------
RP_TEST(selected_candidate_never_infeasible) {
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  make_world(planner);
  RecoveryRequest req = make_request();

  auto restore = make_candidate(RecoveryStrategy::RESTORE, 1);
  auto restart = make_candidate(RecoveryStrategy::RESTART, 2);
  restart.cost->expected_time = Duration(100);
  auto decision = planner.plan(req, {restore, restart});
  RP_CHECK(decision.outcome == PlanOutcome::PLAN_FOUND);
  for (const auto& rc : decision.ranked) {
    if (rc.candidate.candidate_id == decision.plan->candidate_id) {
      RP_CHECK(rc.feasibility.feasible());
    }
  }
}

// ---------------------------------------------------------------------------
RP_TEST(deadline_monotonic_never_less_feasible) {
  auto ctx = make_context();
  auto candidate = make_candidate(RecoveryStrategy::RESTORE, 1);
  candidate.cost->expected_time = Duration(1000);
  RecoveryRequest req = make_request();
  Timestamp now(0);

  bool prev_ok = false;
  for (std::int64_t d = 500; d <= 5000; d += 250) {
    req.recovery_deadline = Timestamp(d);
    auto de = evaluate_deadline(candidate, req, ctx, now);
    bool ok = (de.outcome != DeadlineOutcome::VIOLATED);
    // Monotonic: once feasible, never becomes less feasible for larger deadline.
    RP_CHECK(!prev_ok || ok);
    prev_ok = ok;
  }
  RP_CHECK(prev_ok);  // large deadline is always feasible
}

// ---------------------------------------------------------------------------
RP_TEST(unknown_resource_never_unconditionally_feasible) {
  auto ctx = make_context();
  auto candidate = make_candidate(RecoveryStrategy::RESTORE, 1);
  candidate.resource->available = std::nullopt;  // unknown availability
  candidate.resource->status = ResourceStatus::UNKNOWN;
  auto ev = evaluate_feasibility(candidate, make_request(), ctx);
  RP_CHECK(!ev.feasible());
  RP_CHECK(ev.result == FeasibilityResult::INSUFFICIENT_EVIDENCE);
}

// ---------------------------------------------------------------------------
RP_TEST(missing_mandatory_evidence_never_feasible) {
  auto ctx = make_context();
  auto candidate = make_candidate(RecoveryStrategy::RESTORE, 1);
  candidate.state = std::nullopt;  // no state evidence
  auto ev = evaluate_feasibility(candidate, make_request(), ctx);
  RP_CHECK(!ev.feasible());
}

// ---------------------------------------------------------------------------
RP_TEST(terminal_plan_stays_terminal) {
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  make_world(planner);
  RecoveryRequest req = make_request();
  auto d = planner.plan(req, {make_candidate(RecoveryStrategy::RESTORE, 1)});
  auto auth = planner.authorize_plan(*d.plan);
  ReferenceExecutor exec(
      [&planner](DispatchId dd, ExecutionOutcome o, ProgressUnits r, Duration e, std::string s) {
        planner.on_adapter_completion(dd, o, r, e, std::move(s));
      });
  auto done = planner.dispatch_plan(auth, exec);
  RP_CHECK(done.state == PlanLifecycleState::SUCCEEDED);

  // A later FAILED report must not un-succeed a terminal plan.
  ExecutionReport rep;
  rep.plan_id = done.plan_id;
  rep.plan_generation = done.generation;
  rep.reporter_boot = done.target_boot;
  rep.outcome = ExecutionOutcome::FAILED;
  planner.report_execution(rep);
  auto _plans = planner.plans(); auto& p = _plans[0];
  RP_CHECK(p.state == PlanLifecycleState::SUCCEEDED);
}

// ---------------------------------------------------------------------------
RP_TEST(exactly_one_authoritative_terminal_success) {
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  make_world(planner);
  RecoveryRequest req = make_request();
  auto d = planner.plan(req, {make_candidate(RecoveryStrategy::RESTORE, 1)});
  auto auth = planner.authorize_plan(*d.plan);
  ReferenceExecutor exec(
      [&planner](DispatchId dd, ExecutionOutcome o, ProgressUnits r, Duration e, std::string s) {
        planner.on_adapter_completion(dd, o, r, e, std::move(s));
      });
  // Dispatch twice (duplicate dispatch) - the second must be refused or harmless.
  auto done = planner.dispatch_plan(auth, exec);
  RP_EXPECT_THROW(planner.dispatch_plan(done, exec), ErrorCode::STALE_AUTHORITY);  // already terminal
  auto _plans = planner.plans(); auto& p = _plans[0];
  RP_CHECK(p.execution_outcome == ExecutionOutcome::SUCCEEDED);
  RP_CHECK(p.outcome_published == true);
}

// ---------------------------------------------------------------------------
RP_TEST(candidate_tie_break_is_deterministic) {
  auto ctx = make_context();
  auto a = make_candidate(RecoveryStrategy::RESTORE, 7);
  auto b = make_candidate(RecoveryStrategy::RESTORE, 3);
  // identical economics -> lower candidate id wins
  auto ea = compute_economics(a, make_request(), ctx, Timestamp(0));
  auto eb = compute_economics(b, make_request(), ctx, Timestamp(0));
  RP_CHECK(candidate_precedes(b, a, eb, ea,
                              evaluate_deadline(b, make_request(), ctx, Timestamp(0)),
                              evaluate_deadline(a, make_request(), ctx, Timestamp(0))));
  RP_CHECK(!candidate_precedes(a, b, ea, eb,
                               evaluate_deadline(a, make_request(), ctx, Timestamp(0)),
                               evaluate_deadline(b, make_request(), ctx, Timestamp(0))));
}

// ---------------------------------------------------------------------------
RP_TEST(progress_loss_tolerance_is_a_hard_constraint) {
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  make_world(planner);

  RecoveryRequest req = make_request();
  req.max_tolerated_progress_loss = ProgressUnits(10);
  auto restore = make_candidate(RecoveryStrategy::RESTORE, 1);
  restore.cost->expected_lost_progress = ProgressUnits(5);
  auto restart = make_candidate(RecoveryStrategy::RESTART, 2);
  restart.cost->expected_lost_progress = ProgressUnits(50);  // violates tolerance

  auto decision = planner.plan(req, {restore, restart});
  RP_CHECK(decision.outcome == PlanOutcome::PLAN_FOUND);
  RP_CHECK(decision.plan->candidate_id == CandidateId(1));
}
