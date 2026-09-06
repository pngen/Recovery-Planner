#include "test_util.hpp"
#include "scenario.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>

using namespace recovery_planner;
using namespace scenario;
namespace fs = std::filesystem;

namespace {
std::string temp_dir() {
  static long counter = 0;
  auto ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  return (fs::temp_directory_path() / ("rplanner_test_" + std::to_string(ts) +
          "_" + std::to_string(counter++))).string();
}
void cleanup(const std::string& dir) {
  std::error_code ec;
  fs::remove_all(dir, ec);
}
std::string frame_path(const std::string& dir, const std::string& key) {
  return (fs::path(dir) / (key + ".rpp")).string();
}
void make_world(RecoveryPlanner& planner) {
  planner.register_worker(kWorkerA, kBootA);
  planner.register_worker(kWorkerB, kBootB);
  planner.set_resource("gpu_mem", 10000);
  planner.set_recompute_available(kWorkloadGen, true);
}
}

// ---------------------------------------------------------------------------
RP_TEST(persistence_save_load_roundtrip) {
  std::string dir = temp_dir();
  auto store = make_file_persistence_store(dir);
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, store);
  make_world(planner);

  RecoveryRequest req = make_request();
  auto decision = planner.plan(req, {make_candidate(RecoveryStrategy::RESTORE, 1)});
  RP_CHECK(decision.outcome == PlanOutcome::PLAN_FOUND);
  planner.save_state();
  std::string digest = decision.plan->decision_digest;

  RecoveryPlanner planner2(clock, store);
  planner2.load_state();
  make_world(planner2);  // workers must republish after load (dynamic evidence)
  RP_CHECK(planner2.context().epoch == kEpoch);
  auto plans = planner2.plans();
  RP_CHECK(plans.size() == 1);
  RP_CHECK(plans[0].decision_digest == digest);

  // Replaying the same durable request + candidate reconstructs the same identity.
  auto d2 = planner2.plan(req, {make_candidate(RecoveryStrategy::RESTORE, 1)});
  RP_CHECK(d2.plan->decision_digest == digest);
  cleanup(dir);
}

// ---------------------------------------------------------------------------
RP_TEST(persistence_corruption_rejected) {
  std::string dir = temp_dir();
  auto store = make_file_persistence_store(dir);
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, store);
  make_world(planner);
  planner.plan(make_request(), {make_candidate(RecoveryStrategy::RESTORE, 1)});
  planner.save_state();

  // Corrupt one payload byte.
  std::string path = frame_path(dir, "planner_state");
  std::vector<std::uint8_t> raw;
  {
    std::ifstream ifs(path, std::ios::binary);
    raw.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
  }
  RP_CHECK(raw.size() > 16);
  raw[raw.size() / 2] ^= 0xFF;  // flip a byte in the payload
  {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ofs.write(reinterpret_cast<const char*>(raw.data()), (std::streamsize)raw.size());
  }
  auto bad = store->load("planner_state");
  RP_CHECK(!bad.has_value());
  cleanup(dir);
}

// ---------------------------------------------------------------------------
RP_TEST(persistence_truncation_rejected) {
  std::string dir = temp_dir();
  auto store = make_file_persistence_store(dir);
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, store);
  make_world(planner);
  planner.plan(make_request(), {make_candidate(RecoveryStrategy::RESTORE, 1)});
  planner.save_state();

  std::string path = frame_path(dir, "planner_state");
  std::vector<std::uint8_t> raw;
  {
    std::ifstream ifs(path, std::ios::binary);
    raw.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
  }
  raw.resize(raw.size() / 2);
  {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ofs.write(reinterpret_cast<const char*>(raw.data()), (std::streamsize)raw.size());
  }
  auto bad = store->load("planner_state");
  RP_CHECK(!bad.has_value());
  cleanup(dir);
}

// ---------------------------------------------------------------------------
RP_TEST(coordinator_restart_invalidates_inflight_and_advances_epoch) {
  std::string dir = temp_dir();
  auto store = make_file_persistence_store(dir);
  auto clock = std::make_shared<TestClock>(Timestamp(0));
  RecoveryPlanner planner(clock, store);
  make_world(planner);
  RecoveryRequest req = make_request();
  auto decision = planner.plan(req, {make_candidate(RecoveryStrategy::RESTORE, 1)});
  auto authorized = planner.authorize_plan(*decision.plan);
  RP_CHECK(authorized.state == PlanLifecycleState::AUTHORIZED);
  planner.save_state();

  CoordinatorEpoch before_epoch = planner.current_epoch();
  planner.restart_coordinator();
  RP_CHECK(planner.current_epoch().value() == before_epoch.value() + 1);

  // The authorized-but-not-completed plan must not be resumed; it is invalidated.
  auto plans = planner.plans();
  bool found_invalidated = false;
  for (const auto& p : plans) {
    if (p.plan_id == decision.plan->plan_id) {
      RP_CHECK(p.state == PlanLifecycleState::INVALIDATED);
      found_invalidated = true;
    }
  }
  RP_CHECK(found_invalidated);

  // After restart, dynamic evidence requires re-registration. New work can plan
  // once workers republish under the new epoch.
  planner.register_worker(kWorkerA, kBootA);
  planner.register_worker(kWorkerB, kBootB);
  planner.set_resource("gpu_mem", 10000);
  planner.set_recompute_available(kWorkloadGen, true);
  CoordinatorEpoch now_epoch = planner.current_epoch();
  auto c = make_candidate(RecoveryStrategy::RESTORE, 1);
  c.authority->coordinator_epoch = now_epoch;
  c.authority->meta.coordinator_epoch = now_epoch;
  RecoveryRequest req2 = make_request();
  auto d2 = planner.plan(req2, {c});
  RP_CHECK(d2.outcome == PlanOutcome::PLAN_FOUND);
  cleanup(dir);
}
