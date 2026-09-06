#pragma once

// RecoveryPlan, its authority binding, its lifecycle, and its explanation.
//
// A plan is an authoritative object fenced to a specific set of generations. It
// must be revalidated before dispatch; a stale plan rejects without mutating
// current state.

#include "recovery_planner/types.hpp"
#include "recovery_planner/evidence.hpp"
#include "recovery_planner/strategy.hpp"
#include "recovery_planner/economics.hpp"
#include "recovery_planner/feasibility.hpp"
#include "recovery_planner/planner_context.hpp"
#include "recovery_planner/executor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace recovery_planner {

enum class PlanLifecycleState : std::uint8_t {
  REQUESTED = 0,
  EVALUATING = 1,
  PLANNED = 2,
  AUTHORIZED = 3,
  DISPATCHED = 4,
  EXECUTING = 5,
  SUCCEEDED = 6,
  FAILED = 7,
  SUPERSEDED = 8,
  CANCELLED = 9,
  EXPIRED = 10,
  INVALIDATED = 11
};

inline const char* to_string(PlanLifecycleState s) noexcept {
  switch (s) {
    case PlanLifecycleState::REQUESTED: return "REQUESTED";
    case PlanLifecycleState::EVALUATING: return "EVALUATING";
    case PlanLifecycleState::PLANNED: return "PLANNED";
    case PlanLifecycleState::AUTHORIZED: return "AUTHORIZED";
    case PlanLifecycleState::DISPATCHED: return "DISPATCHED";
    case PlanLifecycleState::EXECUTING: return "EXECUTING";
    case PlanLifecycleState::SUCCEEDED: return "SUCCEEDED";
    case PlanLifecycleState::FAILED: return "FAILED";
    case PlanLifecycleState::SUPERSEDED: return "SUPERSEDED";
    case PlanLifecycleState::CANCELLED: return "CANCELLED";
    case PlanLifecycleState::EXPIRED: return "EXPIRED";
    case PlanLifecycleState::INVALIDATED: return "INVALIDATED";
  }
  return "UNKNOWN";
}

// The generation fence binding a plan to a specific world.
struct PlanAuthority {
  CoordinatorEpoch coordinator_epoch;
  PlannerBootId planner_boot;
  RecoveryRequestId request_id;
  RecoveryPlanGeneration plan_generation;
  CandidateGeneration candidate_generation;
  WorkerBootId target_boot;
  EngineIncarnationId engine_incarnation;
  StateGeneration state_generation;
  CompatibilityGeneration compatibility_generation;
  ResourceGeneration resource_generation;
  PolicyGeneration policy_generation;
  TopologyGeneration topology_generation;
  WorkloadGeneration workload_generation;
};

// A ranked factor shown in the explanation.
struct RankFactor {
  std::string name;       // stable factor name
  std::string value;      // normalized typed value as a string
  std::uint64_t priority; // deterministic ordering weight
};

// A rejected alternative.
struct RejectedAlternative {
  CandidateId candidate_id;
  RecoveryStrategy strategy{RecoveryStrategy::RESTART};
  FeasibilityResult feasibility;
  std::vector<std::string> reasons;
};

// What would change the decision (computed only after binding constraints).
struct WhatWouldChange {
  std::vector<std::pair<std::string, std::string>> conditions; // condition -> consequence
};

// Structured plan explanation. Never built from ad hoc log strings.
struct PlanExplanation {
  RecoveryStrategy selected_strategy{RecoveryStrategy::RESTART};
  CandidateId selected_candidate;
  CandidateGeneration selected_candidate_generation;
  std::vector<RankFactor> rank_factors;
  std::vector<ConstraintViolation> binding_constraints;
  std::vector<std::string> feasibility_reasons;
  std::optional<Duration> deadline_slack;
  std::optional<ProgressUnits> preserved_progress;
  std::optional<ProgressUnits> lost_progress;
  std::optional<CostUnits> cost;
  CandidateReadiness readiness{CandidateReadiness::UNKNOWN};
  CompatibilityResult compatibility{CompatibilityResult::UNKNOWN};
  ResourceStatus resource_status{ResourceStatus::UNKNOWN};
  Confidence uncertainty;
  PlanAuthority authority;
  std::vector<RejectedAlternative> rejected_alternatives;
  std::vector<std::string> missing_evidence;
  WhatWouldChange what_would_change;
};

// A recovery plan: the authoritative, executable object.
struct RecoveryPlan {
  RecoveryPlanId plan_id;
  RecoveryPlanGeneration generation;
  RecoveryRequestId request_id;
  WorkloadId workload;
  RecoveryStrategy strategy{RecoveryStrategy::RESTART};
  CandidateId candidate_id;
  CandidateGeneration candidate_generation;
  WorkerId target_worker;
  WorkerBootId target_boot;
  EngineId target_engine;
  EngineIncarnationId engine_incarnation;
  StateId state_source;
  StateGeneration state_generation;
  CheckpointId checkpoint_source;
  PlanAuthority authority;
  PlanLifecycleState state{PlanLifecycleState::PLANNED};
  RecoveryEconomics economics;
  DeadlineEvaluation deadline;
  PlanExplanation explanation;
  std::string dispatch_payload;   // typed, adapter-interpreted payload
  Timestamp created_at;
  Timestamp updated_at;
  AttemptId last_attempt;
  ExecutionOutcome execution_outcome{ExecutionOutcome::UNKNOWN};
  bool outcome_published{false};
  std::string decision_digest;   // canonical digest of the decision identity
};

// Validate a lifecycle transition. Returns true if allowed.
bool validate_plan_transition(PlanLifecycleState from, PlanLifecycleState to) noexcept;

// Check whether a plan's authority is still current under the context.
bool plan_authority_current(const RecoveryPlan& plan, const PlannerContext& context) noexcept;

}  // namespace recovery_planner
