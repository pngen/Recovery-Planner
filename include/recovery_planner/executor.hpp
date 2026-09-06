#pragma once

// Narrow execution-adapter surface.
//
// The planner emits typed RecoveryPlan/RecoveryDispatch objects to an executor
// and tracks execution/result authority, but it never fakes execution of
// mechanisms owned by adjacent runtimes. A deterministic reference executor is
// provided for tests and the distributed proof.

#include "recovery_planner/types.hpp"
#include "recovery_planner/strategy.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>

namespace recovery_planner {

enum class DispatchStatus : std::uint8_t {
  DISPATCHED = 0,
  REJECTED = 1,
  DROPPED = 2,
  UNKNOWN = 3
};

inline const char* to_string(DispatchStatus s) noexcept {
  switch (s) {
    case DispatchStatus::DISPATCHED: return "DISPATCHED";
    case DispatchStatus::REJECTED: return "REJECTED";
    case DispatchStatus::DROPPED: return "DROPPED";
    case DispatchStatus::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

enum class ExecutionOutcome : std::uint8_t {
  SUCCEEDED = 0,
  FAILED = 1,
  AMBIGUOUS = 2,
  UNKNOWN = 3,
  CANCELLED = 4
};

inline const char* to_string(ExecutionOutcome o) noexcept {
  switch (o) {
    case ExecutionOutcome::SUCCEEDED: return "SUCCEEDED";
    case ExecutionOutcome::FAILED: return "FAILED";
    case ExecutionOutcome::AMBIGUOUS: return "AMBIGUOUS";
    case ExecutionOutcome::UNKNOWN: return "UNKNOWN";
    case ExecutionOutcome::CANCELLED: return "CANCELLED";
  }
  return "UNKNOWN";
}

// A dispatch instruction emitted by the planner to an adapter.
struct RecoveryDispatch {
  DispatchId dispatch_id;
  RecoveryPlanId plan_id;
  RecoveryPlanGeneration plan_generation;
  RecoveryRequestId request_id;
  RecoveryStrategy strategy{RecoveryStrategy::RESTART};
  WorkerId target_worker;
  WorkerBootId target_boot;
  EngineIncarnationId engine_incarnation;
  StateId state_source;
  StateGeneration state_generation;
  AttemptId attempt;
  std::string payload;   // adapter-interpreted dispatch payload
};

// The receipt returned by an adapter when a dispatch is accepted or rejected.
struct RecoveryReceipt {
  DispatchId dispatch_id;
  DispatchStatus status{DispatchStatus::UNKNOWN};
  Timestamp at;
  std::string detail;
};

// Completion callback signature (used by adapters to report outcomes back).
using CompletionCallback = std::function<void(DispatchId dispatch, ExecutionOutcome outcome,
                                              ProgressUnits recovered, Duration elapsed,
                                              std::string detail)>;

// Narrow adapter interface. The planner dispatches; the adapter performs the
// mechanism and reports completion via the registered callback.
class IRecoveryExecutor {
 public:
  virtual ~IRecoveryExecutor() = default;
  // Start a dispatch. The adapter reports the outcome asynchronously or
  // synchronously via the completion callback.
  virtual RecoveryReceipt dispatch(const RecoveryDispatch& dispatch) = 0;
  virtual const char* name() const noexcept = 0;
};

// A deterministic reference executor. It performs a small, clearly labeled
// SYNTHETIC recovery mechanism in-process and reports completion through the
// callback. Behavior can be overridden per plan id to inject failures,
// duplicate completions, target-death, etc. for adversarial tests.
class ReferenceExecutor final : public IRecoveryExecutor {
 public:
  explicit ReferenceExecutor(CompletionCallback on_complete,
                             Timestamp (*now_fn)() = nullptr);
  ~ReferenceExecutor() override = default;

  // Register a deterministic outcome for a given plan id; subsequent dispatches
  // for that plan report it. This is how tests inject failure-during-recovery.
  void register_outcome(std::uint64_t plan_id, ExecutionOutcome outcome,
                        ProgressUnits recovered, Duration elapsed, std::string detail);

  RecoveryReceipt dispatch(const RecoveryDispatch& dispatch) override;
  const char* name() const noexcept override { return "reference"; }

  // Confirmed dispatch count (for accounting tests).
  std::size_t dispatch_count() const noexcept { return dispatched_; }

 private:
  CompletionCallback on_complete_;
  Timestamp (*now_fn_)(){nullptr};
  std::map<std::uint64_t, std::tuple<ExecutionOutcome, ProgressUnits, Duration, std::string>> overrides_;
  std::size_t dispatched_{0};
};

std::shared_ptr<IRecoveryExecutor> make_reference_executor(CompletionCallback on_complete,
                                                           Timestamp (*now_fn)() = nullptr);

}  // namespace recovery_planner
