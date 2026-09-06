#pragma once

// RecoveryPlanner: the decision layer.
//
// Given a recovery request and a set of recovery candidates, the planner
// validates authority, evaluates freshness/compatibility/feasibility, computes
// recovery economics, applies hard constraints, deterministically ranks the
// feasible candidates, emits an authoritative fenced plan, and tracks its
// guarded lifecycle. A plan must be revalidated before dispatch.

#include "recovery_planner/types.hpp"
#include "recovery_planner/clock.hpp"
#include "recovery_planner/evidence.hpp"
#include "recovery_planner/strategy.hpp"
#include "recovery_planner/candidate.hpp"
#include "recovery_planner/request.hpp"
#include "recovery_planner/feasibility.hpp"
#include "recovery_planner/economics.hpp"
#include "recovery_planner/plan.hpp"
#include "recovery_planner/planner_context.hpp"
#include "recovery_planner/persistence.hpp"
#include "recovery_planner/executor.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace recovery_planner {

enum class PlanOutcome : std::uint8_t {
  PLAN_FOUND = 0,
  NO_VALID_PLAN = 1,                 // all candidates invalid/stale/incompatible/insufficient
  NO_DEADLINE_FEASIBLE_PLAN = 2,     // every candidate misses a hard deadline
  NO_CANDIDATES = 3,
  REVALIDATION_REQUIRED = 4
};

inline const char* to_string(PlanOutcome o) noexcept {
  switch (o) {
    case PlanOutcome::PLAN_FOUND: return "PLAN_FOUND";
    case PlanOutcome::NO_VALID_PLAN: return "NO_VALID_PLAN";
    case PlanOutcome::NO_DEADLINE_FEASIBLE_PLAN: return "NO_DEADLINE_FEASIBLE_PLAN";
    case PlanOutcome::NO_CANDIDATES: return "NO_CANDIDATES";
    case PlanOutcome::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
  }
  return "UNKNOWN";
}

// A candidate evaluated during planning, with its decision-relevant data.
struct RankedCandidate {
  RecoveryCandidate candidate;
  FeasibilityEvaluation feasibility;
  RecoveryEconomics economics;
  DeadlineEvaluation deadline;
  int rank{0};               // 1-based rank among all candidates received
  int feasible_rank{0};      // 1-based rank among feasible candidates
};

// The full decision result.
struct RecoveryDecision {
  PlanOutcome outcome{PlanOutcome::NO_VALID_PLAN};
  std::optional<RecoveryPlan> plan;
  std::vector<RankedCandidate> ranked;      // stable input ordering
  std::vector<std::string> reasons;         // top-level reasons (structured)
  std::string summary;
};

// An execution report reported back by an adapter.
struct ExecutionReport {
  DispatchId dispatch;
  RecoveryPlanId plan_id;
  RecoveryPlanGeneration plan_generation;
  WorkerBootId reporter_boot;     // fencing identity of the reporter
  ExecutionOutcome outcome{ExecutionOutcome::UNKNOWN};
  ProgressUnits recovered;
  Duration elapsed;
  std::string detail;
};

// The decision layer. Thread-safe: concurrent planning, evidence updates, plan
// lifecycle changes and execution reports are serialized on an internal mutex.
// Internal locks are never held across network/backend waits.
class RecoveryPlanner {
 public:
  RecoveryPlanner(std::shared_ptr<Clock> clock,
                  std::shared_ptr<PersistenceStore> store = nullptr);
  ~RecoveryPlanner();

  RecoveryPlanner(const RecoveryPlanner&) = delete;
  RecoveryPlanner& operator=(const RecoveryPlanner&) = delete;

  // ---- planning -----------------------------------------------------------

  // Evaluate a recovery request against the given candidates. Deterministic
  // for identical canonical inputs. Does not mutate current state; records the
  // decision + history.
  RecoveryDecision plan(const RecoveryRequest& request,
                        std::vector<RecoveryCandidate> candidates);

  // ---- fenced plan lifecycle ----------------------------------------------

  // PLANNED -> AUTHORIZED. Rejects if the plan is no longer current.
  RecoveryPlan authorize_plan(const RecoveryPlan& plan);

  // Pre-execution revalidation. Returns a revalidated (still PLANNED/AUTHORIZED)
  // plan, or throws RecoveryError(stale authority). Does not mutate state on
  // rejection.
  RecoveryPlan revalidate_plan(const RecoveryPlan& plan);

  // AUTHORIZED -> DISPATCHED/EXECUTING via an executor. Requires current plan.
  RecoveryPlan dispatch_plan(const RecoveryPlan& plan, IRecoveryExecutor& executor);

  // Report an execution outcome (completion/failure/ambiguity). Exactly one
  // authoritative terminal outcome may survive.
  void report_execution(const ExecutionReport& report);

  // Adapter-friendly completion entry point. Resolves the dispatch id to the
  // fenced plan identity before delegating to report_execution. Safe to call
  // from an adapter callback on any thread.
  void on_adapter_completion(DispatchId dispatch_id, ExecutionOutcome outcome,
                             ProgressUnits recovered, Duration elapsed, std::string detail);

  // CANCELLED at the appropriate boundary. force allows cancelling a dispatched
  // plan only when the commit boundary has not been crossed.
  bool cancel_plan(RecoveryPlanId plan_id, bool force = false);

  void invalidate_plan(RecoveryPlanId plan_id);

  // ---- world maintenance ---------------------------------------------------

  void register_worker(WorkerId w, WorkerBootId boot);
  void unregister_worker(WorkerId w);
  void set_resource(const std::string& kind, std::uint64_t amount);
  void set_recompute_available(WorkloadGeneration gen, bool available);

  // ---- persistence + restart ------------------------------------------------

  bool save_state();
  bool load_state();
  // Coordinator restart: persist current epoch, create a new boot id, reload,
  // advance epoch, and mark all dynamic evidence REVALIDATION_REQUIRED.
  void restart_coordinator();

  // ---- inspection -----------------------------------------------------------

  const PlannerContext& context() const;
  CoordinatorEpoch current_epoch() const;
  std::vector<RecoveryPlan> plans() const;
  std::size_t history_size() const;
  std::vector<std::string> history_summaries() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace recovery_planner
