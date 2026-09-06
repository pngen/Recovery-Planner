#pragma once

// Deterministic feasibility evaluation.
//
// A candidate may be FEASIBLE, INFEASIBLE, DEFER (e.g. warming but not ready),
// REVALIDATION_REQUIRED (evidence too stale to decide now), or
// INSUFFICIENT_EVIDENCE (mandatory evidence absent). UNKNOWN never becomes
// FEASIBLE by default.

#include "recovery_planner/types.hpp"
#include "recovery_planner/candidate.hpp"
#include "recovery_planner/request.hpp"
#include "recovery_planner/planner_context.hpp"

#include <string>
#include <vector>

namespace recovery_planner {

enum class FeasibilityResult : std::uint8_t {
  FEASIBLE = 0,
  INFEASIBLE = 1,
  DEFER = 2,
  REVALIDATION_REQUIRED = 3,
  INSUFFICIENT_EVIDENCE = 4
};

inline const char* to_string(FeasibilityResult r) noexcept {
  switch (r) {
    case FeasibilityResult::FEASIBLE: return "FEASIBLE";
    case FeasibilityResult::INFEASIBLE: return "INFEASIBLE";
    case FeasibilityResult::DEFER: return "DEFER";
    case FeasibilityResult::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
    case FeasibilityResult::INSUFFICIENT_EVIDENCE: return "INSUFFICIENT_EVIDENCE";
  }
  return "UNKNOWN";
}

// Reason codes for a binding constraint / rejection.
enum class RejectionReason : std::uint8_t {
  NONE = 0,
  STALE_AUTHORITY = 1,
  INVALID_STATE_INTEGRITY = 2,
  INCOMPATIBLE = 3,
  TARGET_UNAVAILABLE = 4,
  INVALID_CANDIDATE_GENERATION = 5,
  INSUFFICIENT_RESOURCES = 6,
  EXPIRED_STATE = 7,
  STATE_TOO_OLD = 8,
  DEADLINE_IMPOSSIBLE = 9,
  FORBIDDEN_STRATEGY = 10,
  MISSING_EVIDENCE = 11,
  TARGET_INCARNATION_MISMATCH = 12,
  READINESS_FAILED = 13,
  TOPOLOGY_IMPOSSIBLE = 14,
  INCOMPATIBLE_CHECKPOINT = 15,
  INVALID_MIGRATION_RELATION = 16,
  SHADOW_NOT_CURRENT = 17,
  NO_RECOMPUTE_LINEAGE = 18,
  AMBIGUOUS_UNSAFE = 19,
  RESOURCE_GENERATION_STALE = 20,
  COMPATIBILITY_GENERATION_STALE = 21,
  POLICY_GENERATION_STALE = 22,
  WORKLOAD_GENERATION_STALE = 23,
  INVALID_STRATEGY = 24,
  STATE_GENERATION_STALE = 25
};

inline const char* to_string(RejectionReason r) noexcept {
  switch (r) {
    case RejectionReason::NONE: return "none";
    case RejectionReason::STALE_AUTHORITY: return "stale_authority";
    case RejectionReason::INVALID_STATE_INTEGRITY: return "invalid_state_integrity";
    case RejectionReason::INCOMPATIBLE: return "incompatible";
    case RejectionReason::TARGET_UNAVAILABLE: return "target_unavailable";
    case RejectionReason::INVALID_CANDIDATE_GENERATION: return "invalid_candidate_generation";
    case RejectionReason::INSUFFICIENT_RESOURCES: return "insufficient_resources";
    case RejectionReason::EXPIRED_STATE: return "expired_state";
    case RejectionReason::STATE_TOO_OLD: return "state_too_old";
    case RejectionReason::DEADLINE_IMPOSSIBLE: return "deadline_impossible";
    case RejectionReason::FORBIDDEN_STRATEGY: return "forbidden_strategy";
    case RejectionReason::MISSING_EVIDENCE: return "missing_evidence";
    case RejectionReason::TARGET_INCARNATION_MISMATCH: return "target_incarnation_mismatch";
    case RejectionReason::READINESS_FAILED: return "readiness_failed";
    case RejectionReason::TOPOLOGY_IMPOSSIBLE: return "topology_impossible";
    case RejectionReason::INCOMPATIBLE_CHECKPOINT: return "incompatible_checkpoint";
    case RejectionReason::INVALID_MIGRATION_RELATION: return "invalid_migration_relation";
    case RejectionReason::SHADOW_NOT_CURRENT: return "shadow_not_current";
    case RejectionReason::NO_RECOMPUTE_LINEAGE: return "no_recompute_lineage";
    case RejectionReason::AMBIGUOUS_UNSAFE: return "ambiguous_unsafe";
    case RejectionReason::RESOURCE_GENERATION_STALE: return "resource_generation_stale";
    case RejectionReason::COMPATIBILITY_GENERATION_STALE: return "compatibility_generation_stale";
    case RejectionReason::POLICY_GENERATION_STALE: return "policy_generation_stale";
    case RejectionReason::WORKLOAD_GENERATION_STALE: return "workload_generation_stale";
    case RejectionReason::INVALID_STRATEGY: return "invalid_strategy";
    case RejectionReason::STATE_GENERATION_STALE: return "state_generation_stale";
  }
  return "unknown";
}

struct ConstraintViolation {
  RejectionReason reason{RejectionReason::NONE};
  std::string detail;
};

struct FeasibilityEvaluation {
  FeasibilityResult result{FeasibilityResult::INSUFFICIENT_EVIDENCE};
  std::vector<ConstraintViolation> violations;

  bool feasible() const noexcept { return result == FeasibilityResult::FEASIBLE; }
  bool blocked() const noexcept { return result == FeasibilityResult::INFEASIBLE; }
  bool can_plan() const noexcept { return feasible() || result == FeasibilityResult::REVALIDATION_REQUIRED; }
};

// Evaluate a single candidate against the request and current planner context.
FeasibilityEvaluation evaluate_feasibility(const RecoveryCandidate& candidate,
                                           const RecoveryRequest& request,
                                           const PlannerContext& context);

}  // namespace recovery_planner
