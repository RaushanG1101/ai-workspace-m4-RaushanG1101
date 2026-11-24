# M5 – Thread-Safe Design

## 1. M4 Fixes

The M4 and CR-1 baseline passed its existing functional tests. Fixed-sleep assumptions in the inherited tests were replaced with bounded lifecycle synchronization, addressing the M4 grading feedback.

## 2. Threads and Shared Data

The GUI thread owns `MainWindow`, `Workspace`, `ProcessingCore`, `workspace_revision_`, widgets, and State objects. `CorpusBuildJob` creates one persistent `std::thread`. `RemoteAddServer` retains its separate IPC thread.

`CorpusBuildJob::mutex_` protects `pending_`, `result_`, `active_`, and `idle_`, accessed by the GUI and worker threads. `Request::cancel` points to an atomic cancellation flag, and `stopping_` is atomic. `condition_` coordinates request availability, cancellation, simulated processing delays, and shutdown. `lifecycle_condition_` supports bounded observation through `wait_until_active()` and `wait_until_idle()`.

The worker processes a private `Workspace` snapshot and constructs a private `ProcessingCore`. Ownership transfers through `result_`. The queued `completed()` signal invokes `check_build_completion()` on the GUI thread.

`RemoteAddServer::stopping_` is atomic, written by `stop()` and read by the IPC thread. The listening descriptor remains unchanged until the thread joins. Remote document paths reach the GUI through a queued signal.

## 3. State Pattern

`MainWindow` delegates lifecycle events to its current polymorphic State object.

| State | Meaning | Build | Add | Reset | Completion |
|---|---|---|---|---|---|
| EmptyState | No documents | Ignored | Dirty | Empty | Ignored |
| DirtyState | Outdated corpus | Building | Dirty | Empty | Ignored |
| BuildingState | Build requested | Ignored | Dirty, cancel | Empty, cancel | Ready or Dirty on failure |
| ReadyState | Current corpus | Building | Dirty | Empty | Ignored |

Exit delegates to `State::exit()`. Confirmed exit destroys `MainWindow` and joins its workers. `update_action_states()` uses `can_build()` and `corpus_current()` to control available operations.

## 4. Event Sequence: Reset During Build

1. `DirtyState::build()` calls `start_build()`, submitting a Workspace snapshot and revision.
2. `CorpusBuildJob::run()` accepts the request under `mutex_` and processes it outside the lock.
3. Reset increments `workspace_revision_`. `BuildingState::reset()` invokes `cancel_build()`.
4. `request_cancel()` clears `pending_` and `result_`, sets the active cancellation token, and notifies `condition_`.
5. The worker wakes from its simulated delay. `ProcessingCore::rebuild_interruptible()` and `CorpusIndex::build_interruptible()` stop obsolete processing.
6. The cancelled result is not published. The worker accepts the next request. `check_build_completion()` additionally rejects mismatched revisions.

## 5. Design Decisions

**Build during build:** `BuildingState` inherits the default no-op `build()`. `update_action_states()` disables Build until completion or invalidation.

**Remote add:** `add_document_from_ipc()` calls `add_document_file()`. `invalidate_corpus()` delegates to `BuildingState::changed()`, cancelling the obsolete build and transitioning to Dirty.

**Shared data:** Only request and result handoff, cancellation, lifecycle observation, and shutdown information are shared. Private snapshots and ownership transfer avoid sharing mutable corpus data.

## 6. Fault Detection

- `AIWS_FAULT_NO_CANCEL`: `CorpusBuildJob::run()` ignores request cancellation. `cancelled_build_does_not_delay_next_build()` detects delayed rebuilding.
- `AIWS_FAULT_SILENT_SHUTDOWN`: The `CorpusBuildJob` destructor omits notification. `idle_worker_shutdown()` targets the blocked join; CTest bounds the suite to 45 seconds.

The final normal suite passed in 3.90 seconds. Before minor test-source corrections, the cancellation fault failed in 10.99 seconds and the shutdown fault timed out in 45.04 seconds.

## 7. AI Disclosure

ChatGPT assisted with implementation, synchronization, State pattern design, testing, debugging, and documentation. I integrated the changes and executed the builds and tests in the course Docker environment.