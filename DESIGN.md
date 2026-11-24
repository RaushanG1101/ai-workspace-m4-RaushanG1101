# M5 - Thread-Safe Design

## 1. M4 Fixes

The M4 and CR-1 baseline passed all existing functional tests. Fixed waiting periods in the inherited tests were replaced with bounded lifecycle synchronization to address the timing issues identified in the M4 grading feedback.

## 2. Threads and Shared Data

The GUI thread owns MainWindow, Workspace, ProcessingCore, workspace_revision_, widgets, and State objects. CorpusBuildJob creates one persistent std::thread, while RemoteAddServer maintains a separate IPC thread.

CorpusBuildJob::mutex_ protects the following shared variables:

- pending_: The GUI submits build requests, and the worker retrieves them.
- result_: The worker publishes completed results, and the GUI retrieves them.
- active_: The worker identifies the active request, and the GUI accesses its cancellation token.
- idle_: The worker updates its idle status, which the lifecycle tests observe.

Request::cancel points to an atomic cancellation flag. The GUI sets this flag, and the worker checks it during processing. The atomic stopping_ flag communicates shutdown.

The condition_ variable coordinates requests, cancellation, processing delays, and shutdown. The lifecycle_condition_ variable allows tests to observe worker activity without fixed sleeps.

The worker processes a private Workspace snapshot and constructs a private ProcessingCore. Completed results transfer ownership through result_. The completed() signal invokes check_build_completion() through a queued connection on the GUI thread.

RemoteAddServer::stopping_ is atomic. The GUI writes it through stop(), and the IPC thread reads it. The listening descriptor remains unchanged until the IPC thread joins. Remote document paths are delivered through a queued signal.

## 3. State Pattern

MainWindow uses four separate state classes to manage the corpus lifecycle.

- EmptyState: No documents are present. Build requests are ignored. Adding a document transitions to DirtyState, while Reset keeps the application empty.

- DirtyState: Documents are present, but the corpus is outdated. Build transitions to BuildingState. Adding documents keeps the state dirty, and Reset returns to EmptyState.

- BuildingState: A build is in progress. Additional Build requests are ignored. Adding a document cancels the build and transitions to DirtyState. Reset cancels the build and returns to EmptyState. Successful completion transitions to ReadyState, while failure returns to DirtyState.

- ReadyState: The corpus matches the current workspace. Search, term inspection, and context building are available when their input requirements are met. Build transitions to BuildingState, adding documents transitions to DirtyState, and Reset returns to EmptyState.

Completion events are ignored outside BuildingState. All states support Exit through State::exit(). Confirmed exit destroys MainWindow and joins its worker threads.

MainWindow delegates events to the current state object. The update_action_states() function uses can_build() and corpus_current() to control available GUI operations.

## 4. Event Sequence: Reset During Build

1. DirtyState::build() calls start_build(), which submits a Workspace snapshot and revision.

2. CorpusBuildJob::run() retrieves the request under mutex_ and processes it outside the lock.

3. Reset increments workspace_revision_. BuildingState::reset() calls cancel_build().

4. The request_cancel() function clears pending_ and result_, sets the active cancellation flag, and notifies condition_.

5. The worker wakes from its simulated delay. ProcessingCore::rebuild_interruptible() and CorpusIndex::build_interruptible() stop obsolete processing.

6. The cancelled result is not published, and the worker becomes available for the next request. The check_build_completion() function also rejects results with mismatched revisions.

## 5. Design Decisions

Build during build:

BuildingState inherits the default no-op build() function. The update_action_states() function disables Build until completion or invalidation, preventing competing builds.

Remote add during build:

The add_document_from_ipc() function calls add_document_file(). The invalidate_corpus() function delegates to BuildingState::changed(), which cancels the obsolete build and transitions to DirtyState.

Minimizing shared data:

Only request and result handoff, cancellation, lifecycle observation, and shutdown information are shared. Private Workspace snapshots and ProcessingCore ownership transfer prevent concurrent modification of corpus data.

## 6. Fault Detection

AIWS_FAULT_NO_CANCEL:

CorpusBuildJob::run() ignores request cancellation when this fault is enabled. The cancelled_build_does_not_delay_next_build() test detects the resulting delay in processing the replacement corpus.

AIWS_FAULT_SILENT_SHUTDOWN:

The CorpusBuildJob destructor omits the condition variable notification when this fault is enabled. The idle_worker_shutdown() test targets the resulting blocked join. CTest limits the test suite to 45 seconds.

Test results:

- Normal configuration: All tests passed in 3.90 seconds.
- Cancellation fault: Expected failure in 10.99 seconds.
- Silent shutdown fault: Expected timeout in 45.04 seconds.

The fault configurations were tested before minor filename and formatting corrections to the test source. The normal configuration was rebuilt and passed after those corrections.

## 7. AI Disclosure

ChatGPT assisted with implementation, synchronization, State pattern design, test development, debugging, and documentation. I integrated the changes and executed the builds and tests in the course Docker environment.