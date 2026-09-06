#include "test_util.hpp"
#include "scenario.hpp"

using namespace recovery_planner;
using namespace scenario;

namespace {

void make_planner(RecoveryPlanner& planner) {
  planner.register_worker(kWorkerA, kBootA);
  planner.register_worker(kWorkerB, kBootB);
  planner.set_resource("gpu_mem", 10000);
  planner.set_recompute_available(kWorkloadGen, true);
}

RecoveryCandidate with_time(RecoveryCandidate c, Duration time, ProgressUnits lost,
                            ProgressUnits preserved) {
  c.cost->expected_time = time;
  c.cost->expected_lost_progress = lost;
  c.cost->expected_preserved_progress = preserved;
  return c;
}

}  // namespace

// ---------------------------------------------------------------------------
RP_TEST(restore_preferred_over_restart) {
  auto store = make_memory_persistence_store();
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, store);
  make_planner(planner);

  RecoveryRequest req = make_request();
  auto restore = make_candidate(RecoveryStrategy::RESTORE, 1);
  restore = with_time(std::move(restore), Duration(3000), ProgressUnits(0), ProgressUnits(100));
  auto restart = make_candidate(RecoveryStrategy::RESTART, 2);
  restart = with_time(std::move(restart), Duration(3000), ProgressUnits(20), ProgressUnits(80));

  auto decision = planner.plan(req, {restore, restart});
  RP_CHECK(decision.outcome == PlanOutcome::PLAN_FOUND);
  RP_CHECK(decision.plan.has_value());
  RP_CHECK(decision.plan->strategy == RecoveryStrategy::RESTORE);
  RP_CHECK(decision.plan->candidate_id == CandidateId(1));
}

// ---------------------------------------------------------------------------
RP_TEST(deadline_changes_winner) {
  auto store = make_memory_persistence_store();
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, store);
  make_planner(planner);

  auto restore = make_candidate(RecoveryStrategy::RESTORE, 1);
  restore = with_time(std::move(restore), Duration(200000), ProgressUnits(0), ProgressUnits(100));
  auto restart = make_candidate(RecoveryStrategy::RESTART, 2);
  restart = with_time(std::move(restart), Duration(1000), ProgressUnits(90), ProgressUnits(0));

  // Tight deadline: only restart fits.
  RecoveryRequest tight = make_request();
  tight.recovery_deadline = Timestamp(5000);
  auto d1 = planner.plan(tight, {restore, restart});
  RP_CHECK(d1.outcome == PlanOutcome::PLAN_FOUND);
  RP_CHECK(d1.plan->strategy == RecoveryStrategy::RESTART);

  // Loose deadline: restore preserves progress and is preferred.
  RecoveryRequest loose = make_request();
  loose.recovery_deadline = Timestamp(1000000000);
  auto d2 = planner.plan(loose, {restore, restart});
  RP_CHECK(d2.outcome == PlanOutcome::PLAN_FOUND);
  RP_CHECK(d2.plan->strategy == RecoveryStrategy::RESTORE);
}

// ---------------------------------------------------------------------------
RP_TEST(deterministic_selection_independent_of_input_order) {
  auto store = make_memory_persistence_store();
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, store);
  make_planner(planner);

  RecoveryRequest req = make_request();
  auto restore = make_candidate(RecoveryStrategy::RESTORE, 1);
  auto migrate = make_candidate(RecoveryStrategy::MIGRATE, 2);
  auto recompute = make_candidate(RecoveryStrategy::RECOMPUTE, 3);
  auto restart = make_candidate(RecoveryStrategy::RESTART, 4);

  auto d1 = planner.plan(req, {restore, migrate, recompute, restart});
  auto d2 = planner.plan(req, {restart, recompute, migrate, restore});
  RP_CHECK(d1.outcome == PlanOutcome::PLAN_FOUND);
  RP_CHECK(d2.outcome == PlanOutcome::PLAN_FOUND);
  RP_CHECK(d1.plan->strategy == d2.plan->strategy);
  RP_CHECK(d1.plan->candidate_id == d2.plan->candidate_id);
  RP_CHECK(d1.plan->decision_digest == d2.plan->decision_digest);
}

// ---------------------------------------------------------------------------
RP_TEST(stale_authority_candidate_is_rejected) {
  auto store = make_memory_persistence_store();
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, store);
  make_planner(planner);

  RecoveryRequest req = make_request();
  auto c1 = make_candidate(RecoveryStrategy::RESTORE, 1);
  c1.authority->coordinator_epoch = CoordinatorEpoch(999);  // stale epoch
  c1.authority->meta.coordinator_epoch = CoordinatorEpoch(999);
  auto c2 = make_candidate(RecoveryStrategy::RESTART, 2);
  c2.authority->coordinator_epoch = CoordinatorEpoch(999);
  c2.authority->meta.coordinator_epoch = CoordinatorEpoch(999);

  auto decision = planner.plan(req, {c1, c2});
  RP_CHECK(decision.outcome == PlanOutcome::NO_VALID_PLAN);
  RP_CHECK(!decision.plan.has_value());
}

// ---------------------------------------------------------------------------
RP_TEST(no_deadline_feasible_plan_when_all_miss) {
  auto store = make_memory_persistence_store();
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, store);
  make_planner(planner);

  RecoveryRequest req = make_request();
  req.recovery_deadline = Timestamp(500);
  auto c1 = make_candidate(RecoveryStrategy::RESTORE, 1);
  c1 = with_time(std::move(c1), Duration(100000), ProgressUnits(0), ProgressUnits(100));
  auto c2 = make_candidate(RecoveryStrategy::RESTART, 2);
  c2 = with_time(std::move(c2), Duration(100000), ProgressUnits(0), ProgressUnits(50));

  auto decision = planner.plan(req, {c1, c2});
  RP_CHECK(decision.outcome == PlanOutcome::NO_DEADLINE_FEASIBLE_PLAN);
  // A clearly-labeled best fallback is still produced.
  RP_CHECK(decision.plan.has_value());
  RP_CHECK(decision.plan->deadline.outcome == DeadlineOutcome::VIOLATED);
}
