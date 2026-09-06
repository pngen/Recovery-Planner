# Recovery Planner

Recovery Planner is an open-source, vendor-neutral C++20 runtime for planning
authoritative recovery across restore, restart, migrate, rehydrate,
shadow-promotion, failover, and recomputation using state availability,
compatibility, deadline, cost, resource, and recovery evidence.

## The systems question

Given a failed, degraded, interrupted, unavailable, or displaced workload, what
recovery strategy is actually valid now? Which candidate can satisfy the
required recovery objective? What evidence supports that conclusion? Under what
authority may the chosen recovery plan execute?

The thesis: **recovery is not synonymous with restart.**

A restart may be invalid. A checkpoint may exist but be incompatible. A failover
candidate may be alive but not ready. A warm engine may be available but stale.
A state replica may be durable but too expensive to restore before the
deadline. Recomputation may be cheaper and faster than movement. Migration may
be possible but violate topology, compatibility, capacity, or authority. Shadow
promotion may be preferable but only if its state is sufficiently current and
its promotion authority is valid. Recovery Planner makes these distinctions
explicit.

## Boundary

Recovery Planner owns recovery **strategy selection** and recovery-plan
authority. It decides which recovery action is valid and preferable. It does
**not** own the complete implementation of neighboring systems:

- **Failover Fabric** — service failover authority, replacement promotion,
  routing cutover, stale route/worker/result fencing, failback, ambiguous
  request publication semantics.
- **Engine Residency** — preparation, readiness, engine incarnation authority,
  standby/activation, serving-use fencing, drain, engine replacement,
  engine-specific recovery.
- **GPU Memory Service** — shared accelerator-memory service semantics, durable
  segment ownership, memory residency, reusable/warm memory segments,
  memory-service recovery and transfer authority.
- **Checkpoint Fabric / Checkpoint Store** — checkpoint capture, durability,
  integrity, storage, restore payload mechanics, checkpoint lineage and replica
  durability.
- **Preemption Fabric** — preemptibility, safe points, quiescence, preservation
  at interruption, resource release, partial progress, resume eligibility.
- **Failure Fabric** — failure classification, ambiguity, retry semantics,
  rollback/compensation, recovery ownership at the generic failure-semantic
  layer.
- **Compatibility Registry** — canonical compatibility identities and rule
  evaluation.
- **State Provenance** — derivation and lineage of reusable machine state.
- **Resource Broker / Capacity / Reservation layers** — lower-level resource
  arbitration or reservation.
- **SLO Fabric** — the next architectural layer; not part of this repository.
  Recovery Planner may accept recovery deadlines/objectives/policy as inputs but
  does not expand into a general SLO enforcement platform.

Neighboring-runtime evidence is modeled through explicit stable interfaces and
types; there is **no** source-level dependency on another Summon Software Labs
repository in the standalone core.

## Recovery strategies

Planned semantics (the runtime never fakes execution of adjacent systems):

- **RESTORE** — recover from a preserved execution checkpoint/state image into a
  valid execution environment.
- **RESTART** — begin the workload/process/engine again from an allowed restart
  boundary, potentially losing some progress.
- **MIGRATE** — move recoverable execution/state to another compatible execution
  target.
- **REHYDRATE** — reconstruct required runtime state from durable/reusable
  components rather than full checkpoint restore.
- **SHADOW_PROMOTE** — promote an already-prepared shadow/standby copy that has
  sufficiently current state and valid authority.
- **FAILOVER** — delegate service replacement/cutover to a failover-capable
  mechanism/candidate where service-level recovery is appropriate.
- **RECOMPUTE** — recreate lost state/progress from an earlier authoritative
  point or original inputs.

## Architecture

The core is a decision layer structured as a small, deliberate public library:

```
include/recovery_planner/   public, value-oriented API (identity, evidence,
                            candidates, feasibility, economics, plan, planner,
                            persistence, executor)
src/                        implementation
tests/                      deterministic unit, property, adversarial, concurrency tests
distributed/                real multiprocess framed-TCP proof (coordinator + workers)
cuda/                       optional real CUDA recovery-economics proof
tools/                      rplanner CLI inspection/planning tool
examples/                   runnable examples
benchmarks/                 honest completed-work benchmarks
```

Key types: `PlannerContext`, `RecoveryRequest`, `RecoveryCandidate`,
`RecoveryEvidence` (typed records), `RecoveryPlan`, `RecoveryDecision`,
`PlanExplanation`, recovery-policy inputs, `PersistenceStore`,
`IRecoveryExecutor`. Identities are strongly typed (`CoordinatorEpoch`,
`WorkerBootId`, `StateGeneration`, `RecoveryPlanGeneration`, …) so that
cross-domain mixing fails at compile time.

## Evidence model

Every decision is evidence-based. No availability, compatibility, freshness,
state integrity, or resource feasibility is inferred merely because a record
exists. Evidence records carry an `EvidenceMeta` with:

- subject identity and generation, observer identity, observation timestamp;
- `EvidenceProvenance`: MEASURED, DERIVED, ESTIMATED, REPORTED, SYNTHETIC,
  UNKNOWN;
- `Freshness`: CURRENT, STALE, REVALIDATION_REQUIRED, EXPIRED, UNKNOWN;
- confidence (fixed-point basis points).

Typed evidence records include `StateEvidence`, `CompatibilityEvidence`,
`AvailabilityEvidence`, `ResourceEvidence`, `TopologyEvidence`,
`TransferEvidence`, `CheckpointEvidence`, `ShadowEvidence`,
`ProgressEvidence`, `DeadlineEvidence`, `CostEvidence`, `AuthorityEvidence`.
Absent evidence is represented by an empty `std::optional` and never becomes
optimistic validity.

## Candidate feasibility

A candidate merely exists; its construction never implies feasibility. The
deterministic feasibility evaluator returns a typed decision:

```
FEASIBLE | INFEASIBLE | DEFER | REVALIDATION_REQUIRED | INSUFFICIENT_EVIDENCE
```

Hard constraints include stale authority, invalid state integrity,
incompatibility, unavailable/not-ready target, invalid candidate generation,
insufficient resources, expired/too-old state, impossible deadline, forbidden
strategy, missing mandatory evidence, target incarnation mismatch, topology
impossibility, invalid migration relation, shadow not current, no recompute
lineage, and ambiguity that makes automatic recovery unsafe. UNKNOWN never
becomes FEASIBLE by default.

## Planning and ranking

The pipeline is:

collect candidate evidence → validate authority → evaluate freshness →
evaluate compatibility → evaluate feasibility → compute recovery economics →
apply hard constraints → rank feasible candidates → deterministic tie-break →
emit plan → require pre-execution revalidation.

Recovery economics expose named factors (expected time, preserved/lost progress,
transfer/restore bytes, cost units, failover-domain risk, confidence) with typed
units; selection is a deterministic lexicographic ordering over named objectives
(preserved progress, lost progress, expected time, cost, risk, confidence, then a
candidate-id/generation tie-break). It is never one unexplained scalar.
Deadline economics compute expected completion, slack, and whether the objective
is hard-feasible, conditionally feasible, or violated. If every candidate misses
a hard deadline the planner returns `NO_DEADLINE_FEASIBLE_PLAN` and still labels
the best fallback clearly as violating the target. Candidate ranking is stable
for identical canonical inputs regardless of input order.

## Authority and generations

A selected plan is an authoritative object fenced to `CoordinatorEpoch`,
`WorkloadGeneration`, `RecoveryRequestId`, `RecoveryPlanGeneration`,
`CandidateGeneration`, `WorkerBootId`, `StateGeneration`,
`CompatibilityGeneration`, `ResourceGeneration`, `PolicyGeneration`, and
`TopologyGeneration`. A plan created under generation N must not execute under
incompatible generation N+1 without explicit revalidation. Stale epoch, boot,
generation, attempt, dispatch, and completion traffic is rejected without
mutating current state. A guarded lifecycle (REQUESTED → EVALUATING → PLANNED →
AUTHORIZED → DISPATCHED → EXECUTING → SUCCEEDED/FAILED) forbids completion
before dispatch, execution without authorization, stale completion after
supersession, duplicate success, cancelled plans later succeeding, and expired
plan dispatch. Pre-execution revalidation re-checks the world; if material state
changed the stale plan is invalidated.

## Persistence and coordinator restart

Durable planner state (requests, plans, generations, worker registrations,
history) is persisted through a `PersistenceStore` as a versioned,
CRC-32C-protected, length-bounded frame committed atomically (temp file +
rename). Truncation, corruption, trailing garbage, unknown version, and
oversized payloads are rejected. On coordinator restart the epoch and boot
advance, dynamic worker/resource evidence is conservatively cleared and must be
re-registered, dynamic evidence loaded from durable state is marked
REVALIDATION_REQUIRED, and any non-terminal (in-flight) plan is invalidated
rather than silently resumed. A canonical decision digest lets identical durable
inputs reconstruct the same decision identity.

## Distributed proof

A real multiprocess proof uses independent OS processes and real framed TCP over
loopback. Frames are magic/version/length bounded and CRC-32C protected, and
survive partial reads/writes, malformed and oversized frames. The coordinator
spawns Worker A and Worker B as real OS processes, awaits registration over TCP,
has Worker A produce durable recovery state, kills Worker A as a real process,
runs the Recovery Planner, and dispatches the selected strategy to Worker B over
the live connection. It exercises RESTORE, RECOMPUTE, and SHADOW_PROMOTE
selection end-to-end with real worker process kill, fresh WorkerBootId fencing,
and stale-completion rejection.

A separate RESTART proof runs the same real multiprocess framing: Worker A is
terminated as a real OS process after producing durable recovery state, a fresh
worker incarnation of the same logical worker is started under a NEW
WorkerBootId, the planner deterministically selects RESTART as the only valid
strategy (no usable checkpoint to restore, no recompute lineage, no READY
shadow), and the restart is dispatched over the live framed-TCP adapter and
completes authoritatively. The proof then shows that stale pre-restart
boot/attempt/dispatch/completion traffic is rejected by the planner's fencing,
leaving exactly one authoritative outcome.

## CUDA proof

An optional CUDA proof runs on real RTX 5090 hardware (CUDA 13.1, sm_120). It
performs real segmented CUDA computation, creates durable recovery state at a
known progress boundary, measures real restore vs recompute cost, uses the
Recovery Planner to select a strategy across two configurations, executes the
selected path, verifies the GPU result against a CPU reference, and returns
device memory to baseline. It demonstrates both a fresh-checkpoint RESTORE win
and a stale-checkpoint RECOMPUTE win.

## REAL vs SYNTHETIC vs UNSUPPORTED

- **REAL** — the distributed proof (real OS processes, real framed TCP, real
  worker process kill, real worker registration/fencing) and the CUDA proof
  (real cudaMalloc, H2D/D2H, kernel, synchronization, CPU parity, device-memory
  baseline) are exercised on the available hardware, process, and network.
- **SYNTHETIC** — the in-process reference executor and the planner's deterministic
  scenario models (cost estimates, shadow lag, recovery mechanics) are labeled as
  such; they model recovery mechanics without fabricating real execution of
  adjacent runtimes.
- **UNSUPPORTED** — hardware checkpointing, transparent GPU migration,
  multi-GPU migration, GPUDirect, NVLink, MIG, and hardware failover are **not**
  implemented or claimed. No fabricated hardware mechanisms are present.

## Examples and CLI

`examples/recovery_examples` demonstrates restore-vs-restart, deadline-driven
recomputation, stale-candidate rejection, incompatible migration, shadow
promotion, rehydration, plan explanation, coordinator restart/revalidation, and
no-valid-plan. `tools/rplanner` is an inspection/planning CLI:

```
rplanner --scenario <RESTORE|RECOMPUTE|SHADOW|MIGRATE|REHYDRATE|FAILOVER|RESTART|NOVALID|DEADLINE>
rplanner --validate <persistence-file>
```

It prints ranked candidates, feasibility, the selected plan, rejection reasons,
deadline slack, and what-would-change analysis.

## Benchmarks

`benchmarks/recovery_planner_bench` measures completed work: candidate
ingestion/feasibility and full plan selection at 8, 64, 256, 1000, and 10000
candidates, concurrent planning, and persistence save/load. It reports
throughput and per-call latency so planner decision latency is distinguished
from actual recovery execution latency.

## Build and install

Requirements: CMake 3.20+, a C++20 compiler, optional CUDA toolkit for the CUDA
proof. Windows (MSVC /W4 /WX) and Linux (Clang/GCC) are supported for the
standalone core.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix <prefix>
```

Optional: `-DRECOVERY_PLANNER_BUILD_CUDA=ON` builds the CUDA proof;
`-DRECOVERY_PLANNER_BUILD_TESTS/EXAMPLES/TOOLS/BENCHMARKS/DISTRIBUTED=OFF`
disable optional targets. AddressSanitizer can be enabled with
`-DCMAKE_CXX_FLAGS="/fsanitize=address /EHsc"`.

## Package consumption

The project installs an exported CMake package. An independent downstream project
consumes it with:

```
find_package(RecoveryPlanner CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE RecoveryPlanner::recovery_planner)
```

## Limitations

- Recovery Planner is a control-plane decision runtime, not a hot numerical
  kernel. It optimizes correctness, determinism, bounded latency, and clear
  explanations.
- The in-process reference executor and the synthetic recovery mechanics are
  SYNTHETIC; real execution of adjacent runtimes (Failover Fabric, Checkpoint
  Store, etc.) is out of scope.
- The CUDA proof demonstrates recovery-economics selection on a single GPU; it
  does not implement multi-GPU migration or live GPU-state transfer.
- No general SLO enforcement is implemented; deadlines/objectives are accepted as
  inputs.
- The distributed proof runs over loopback; multi-node hardware topology is not
  exercised.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
