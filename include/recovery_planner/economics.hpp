#pragma once

// Recovery economics and deadline economics.
//
// Costs are exposed as named factors with typed units, not one opaque scalar.
// Ranking is a deterministic lexicographic ordering over named objectives, and
// a candidate that violates a hard deadline never wins.

#include "recovery_planner/types.hpp"
#include "recovery_planner/candidate.hpp"
#include "recovery_planner/request.hpp"
#include "recovery_planner/planner_context.hpp"

#include <optional>
#include <vector>

namespace recovery_planner {

enum class DeadlineOutcome : std::uint8_t {
  HARD_FEASIBLE = 0,       // expected completion <= deadline
  CONDITIONALLY_FEASIBLE = 1,
  VIOLATED = 2,            // expected completion > deadline
  NO_DEADLINE = 3,         // no deadline imposed
  UNKNOWN = 4
};

inline const char* to_string(DeadlineOutcome o) noexcept {
  switch (o) {
    case DeadlineOutcome::HARD_FEASIBLE: return "HARD_FEASIBLE";
    case DeadlineOutcome::CONDITIONALLY_FEASIBLE: return "CONDITIONALLY_FEASIBLE";
    case DeadlineOutcome::VIOLATED: return "VIOLATED";
    case DeadlineOutcome::NO_DEADLINE: return "NO_DEADLINE";
    case DeadlineOutcome::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

struct DeadlineEvaluation {
  std::optional<Timestamp> deadline;
  std::optional<Timestamp> expected_completion;
  std::optional<Duration> slack;   // expected_completion - deadline; negative => violated
  DeadlineOutcome outcome{DeadlineOutcome::UNKNOWN};
};

// Named cost/time factors. std::nullopt means "unknown", never zero.
struct RecoveryEconomics {
  std::optional<Duration> expected_time;      // total expected recovery latency
  std::optional<Duration> restore_time;
  std::optional<Duration> restart_time;
  std::optional<Duration> transfer_duration;
  std::optional<Duration> recompute_time;
  std::optional<Duration> rehydrate_time;
  std::optional<ByteCount> transfer_bytes;
  std::optional<ByteCount> restore_bytes;
  std::optional<ByteCount> materialize_bytes;
  std::optional<ProgressUnits> expected_lost_progress;
  std::optional<ProgressUnits> expected_preserved_progress;
  std::optional<CostUnits> cost_units;
  std::optional<Duration> checkpoint_age;
  std::optional<Duration> deadline_slack;
  std::optional<ProgressUnits> max_tolerated_progress_loss;
  bool progress_loss_within_tolerance{true};
  std::uint8_t failover_domain_risk{0};
  Confidence confidence;                 // confidence in the cost estimate itself
};

// Evaluate the deadline for a candidate given the current time.
DeadlineEvaluation evaluate_deadline(const RecoveryCandidate& candidate,
                                     const RecoveryRequest& request,
                                     const PlannerContext& context,
                                     Timestamp now);

// Compute the full economics for a candidate.
RecoveryEconomics compute_economics(const RecoveryCandidate& candidate,
                                    const RecoveryRequest& request,
                                    const PlannerContext& context,
                                    Timestamp now);

// Deterministic lexicographic ranking comparator over feasible candidates.
// Returns true if lhs is strictly better than rhs under the documented ordering.
// Priority order: preserved progress, lost progress, expected time, cost, risk,
// confidence, then a stable candidate-id/generation tie-break.
bool candidate_precedes(const RecoveryCandidate& lhs, const RecoveryCandidate& rhs,
                        const RecoveryEconomics& le, const RecoveryEconomics& re,
                        const DeadlineEvaluation& ld, const DeadlineEvaluation& rd);

}  // namespace recovery_planner
