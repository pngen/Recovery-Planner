#include "recovery_planner/executor.hpp"

namespace recovery_planner {

ReferenceExecutor::ReferenceExecutor(CompletionCallback on_complete, Timestamp (*now_fn)())
    : on_complete_(std::move(on_complete)), now_fn_(now_fn) {}

void ReferenceExecutor::register_outcome(std::uint64_t plan_id, ExecutionOutcome outcome,
                                         ProgressUnits recovered, Duration elapsed,
                                         std::string detail) {
  overrides_[plan_id] = std::make_tuple(outcome, recovered, elapsed, std::move(detail));
}

RecoveryReceipt ReferenceExecutor::dispatch(const RecoveryDispatch& dispatch) {
  ++dispatched_;
  Timestamp at = now_fn_ ? Timestamp(now_fn_().value()) : Timestamp(0);

  ExecutionOutcome outcome = ExecutionOutcome::SUCCEEDED;
  ProgressUnits recovered{0};
  Duration elapsed{0};
  std::string detail = "synthetic reference execution";

  auto it = overrides_.find(dispatch.plan_id.value());
  if (it != overrides_.end()) {
    std::tie(outcome, recovered, elapsed, detail) = it->second;
  }

  RecoveryReceipt receipt{dispatch.dispatch_id, DispatchStatus::DISPATCHED, at, "accepted"};
  if (on_complete_) {
    on_complete_(dispatch.dispatch_id, outcome, recovered, elapsed, detail);
  }
  return receipt;
}

std::shared_ptr<IRecoveryExecutor> make_reference_executor(CompletionCallback on_complete,
                                                           Timestamp (*now_fn)()) {
  return std::make_shared<ReferenceExecutor>(std::move(on_complete), now_fn);
}

}  // namespace recovery_planner
