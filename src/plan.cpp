#include "recovery_planner/plan.hpp"

namespace recovery_planner {

bool validate_plan_transition(PlanLifecycleState from, PlanLifecycleState to) noexcept {
  using S = PlanLifecycleState;
  // Terminal states never transition onward.
  auto terminal = [](S s) {
    return s == S::SUCCEEDED || s == S::FAILED || s == S::CANCELLED ||
           s == S::EXPIRED || s == S::INVALIDATED || s == S::SUPERSEDED;
  };
  if (terminal(from)) return false;
  if (to == from) return true;  // idempotent no-op is allowed by callers only after checks

  switch (from) {
    case S::REQUESTED:
      return to == S::EVALUATING || to == S::PLANNED || to == S::CANCELLED || to == S::INVALIDATED;
    case S::EVALUATING:
      return to == S::PLANNED || to == S::CANCELLED || to == S::INVALIDATED;
    case S::PLANNED:
      return to == S::AUTHORIZED || to == S::CANCELLED || to == S::INVALIDATED ||
             to == S::EXPIRED || to == S::SUPERSEDED;
    case S::AUTHORIZED:
      return to == S::DISPATCHED || to == S::CANCELLED || to == S::INVALIDATED ||
             to == S::EXPIRED || to == S::SUPERSEDED;
    case S::DISPATCHED:
      // A dispatched plan may not be "completed before dispatch"; success only
      // after dispatch. It can still be invalidated/superseded.
      return to == S::EXECUTING || to == S::FAILED || to == S::SUCCEEDED ||
             to == S::INVALIDATED || to == S::SUPERSEDED;
    case S::EXECUTING:
      return to == S::SUCCEEDED || to == S::FAILED || to == S::INVALIDATED ||
             to == S::SUPERSEDED;
    default:
      return false;
  }
}

bool plan_authority_current(const RecoveryPlan& plan, const PlannerContext& ctx) noexcept {
  const auto& a = plan.authority;
  if (a.coordinator_epoch != ctx.epoch) return false;
  if (a.policy_generation != ctx.policy_generation) return false;
  if (a.compatibility_generation != ctx.compatibility_generation) return false;
  if (a.resource_generation != ctx.resource_generation) return false;
  if (a.topology_generation != ctx.topology_generation) return false;
  if (a.workload_generation != ctx.workload_generation) return false;
  // Target must still be current: if registered, boot must match; if the worker
  // is not registered/alive, the plan can no longer be current.
  if (!ctx.worker_is_current(plan.target_worker)) return false;
  if (!ctx.worker_boot_matches(plan.target_worker, plan.target_boot)) return false;
  return true;
}

}  // namespace recovery_planner
