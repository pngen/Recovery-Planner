#include "test_util.hpp"
#include "scenario.hpp"

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
RP_TEST(authorize_revalidate_dispatch_success) {
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  make_world(planner);

  RecoveryRequest req = make_request();
  auto restore = make_candidate(RecoveryStrategy::RESTORE, 1);
  auto decision = planner.plan(req, {restore});
  RP_CHECK(decision.outcome == PlanOutcome::PLAN_FOUND);
  RP_CHECK(decision.plan->state == PlanLifecycleState::PLANNED);
  auto plan = *decision.plan;

  auto authorized = planner.authorize_plan(plan);
  RP_CHECK(authorized.state == PlanLifecycleState::AUTHORIZED);

  auto revalidated = planner.revalidate_plan(authorized);
  RP_CHECK(revalidated.state == PlanLifecycleState::AUTHORIZED);

  ReferenceExecutor exec(
      [&planner](DispatchId d, ExecutionOutcome o, ProgressUnits r, Duration e, std::string s) {
        planner.on_adapter_completion(d, o, r, e, std::move(s));
      });
  auto dispatched = planner.dispatch_plan(revalidated, exec);
  RP_CHECK(dispatched.state == PlanLifecycleState::SUCCEEDED);
  RP_CHECK(dispatched.execution_outcome == ExecutionOutcome::SUCCEEDED);
  RP_CHECK(dispatched.outcome_published == true);
}

// ---------------------------------------------------------------------------
RP_TEST(duplicate_completion_rejected) {
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  make_world(planner);

  RecoveryRequest req = make_request();
  auto decision = planner.plan(req, {make_candidate(RecoveryStrategy::RESTORE, 1)});
  auto authorized = planner.authorize_plan(*decision.plan);
  ReferenceExecutor exec(
      [&planner](DispatchId d, ExecutionOutcome o, ProgressUnits r, Duration e, std::string s) {
        planner.on_adapter_completion(d, o, r, e, std::move(s));
      });
  auto dispatched = planner.dispatch_plan(authorized, exec);
  RP_CHECK(dispatched.state == PlanLifecycleState::SUCCEEDED);

  // Conflicting duplicate success from the correct identity must be ignored.
  ExecutionReport rep;
  rep.dispatch = DispatchId(99);
  rep.plan_id = dispatched.plan_id;
  rep.plan_generation = dispatched.generation;
  rep.reporter_boot = dispatched.target_boot;
  rep.outcome = ExecutionOutcome::SUCCEEDED;
  planner.report_execution(rep);
  auto after = *planner.plans().begin();
  RP_CHECK(after.state == PlanLifecycleState::SUCCEEDED);
  RP_CHECK(after.execution_outcome == ExecutionOutcome::SUCCEEDED);
}

// ---------------------------------------------------------------------------
RP_TEST(stale_completion_rejected) {
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  make_world(planner);

  RecoveryRequest req = make_request();
  auto decision = planner.plan(req, {make_candidate(RecoveryStrategy::RESTORE, 1)});
  auto authorized = planner.authorize_plan(*decision.plan);
  // An executor that does NOT report (dispatch stays EXECUTING); use a no-op.
  ReferenceExecutor exec([](DispatchId, ExecutionOutcome, ProgressUnits, Duration, std::string) {});
  auto dispatched = planner.dispatch_plan(authorized, exec);
  RP_CHECK(dispatched.state == PlanLifecycleState::EXECUTING);

  // Stale reporter boot id -> rejected, plan unchanged.
  ExecutionReport rep;
  rep.plan_id = dispatched.plan_id;
  rep.plan_generation = dispatched.generation;
  rep.reporter_boot = WorkerBootId(999999);  // stale
  rep.outcome = ExecutionOutcome::SUCCEEDED;
  planner.report_execution(rep);
  auto _plans = planner.plans(); auto& after = _plans[0];
  RP_CHECK(after.state == PlanLifecycleState::EXECUTING);
}

// ---------------------------------------------------------------------------
RP_TEST(stale_plan_rejected_on_revalidate) {
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  make_world(planner);

  RecoveryRequest req = make_request();
  auto decision = planner.plan(req, {make_candidate(RecoveryStrategy::RESTORE, 1)});
  auto authorized = planner.authorize_plan(*decision.plan);

  // The target worker disappears / restarts under a new boot.
  planner.unregister_worker(kWorkerA);
  RP_EXPECT_THROW(planner.revalidate_plan(authorized), ErrorCode::STALE_AUTHORITY);
}

// ---------------------------------------------------------------------------
RP_TEST(cancel_plan_before_dispatch) {
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  make_world(planner);

  RecoveryRequest req = make_request();
  auto decision = planner.plan(req, {make_candidate(RecoveryStrategy::RESTART, 1)});
  auto plan = *decision.plan;
  bool ok = planner.cancel_plan(plan.plan_id);
  RP_CHECK(ok);
  auto _plans = planner.plans(); auto& after = _plans[0];
  RP_CHECK(after.state == PlanLifecycleState::CANCELLED);
}

// ---------------------------------------------------------------------------
RP_TEST(late_completion_from_invalidated_plan_rejected) {
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  make_world(planner);

  RecoveryRequest req = make_request();
  auto decision = planner.plan(req, {make_candidate(RecoveryStrategy::RESTORE, 1)});
  auto authorized = planner.authorize_plan(*decision.plan);
  planner.invalidate_plan(authorized.plan_id);

  ReferenceExecutor exec(
      [&planner](DispatchId d, ExecutionOutcome o, ProgressUnits r, Duration e, std::string s) {
        planner.on_adapter_completion(d, o, r, e, std::move(s));
      });
  // Dispatch a now-invalidated plan must refuse.
  RP_EXPECT_THROW(planner.dispatch_plan(authorized, exec), ErrorCode::STALE_AUTHORITY);
}

// ---------------------------------------------------------------------------
RP_TEST(new_plan_supersedes_older_plan) {
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, make_memory_persistence_store());
  make_world(planner);

  RecoveryRequest req = make_request();
  auto d1 = planner.plan(req, {make_candidate(RecoveryStrategy::RESTORE, 1)});
  auto d2 = planner.plan(req, {make_candidate(RecoveryStrategy::RESTART, 2)});
  RP_CHECK(d1.plan->candidate_id != d2.plan->candidate_id);

  // The older plan is superseded and can no longer be authorized.
  RP_EXPECT_THROW(planner.authorize_plan(*d1.plan), ErrorCode::INVALID_INPUT);
  auto _plans = planner.plans(); auto& after = _plans[0];
  RP_CHECK(after.state == PlanLifecycleState::SUPERSEDED);
}
