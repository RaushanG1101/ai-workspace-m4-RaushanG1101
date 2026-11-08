# M4 Design Checkpoint — Responsive AI Workspace

## 1. Components and threads

The GUI thread owns MainWindow, the live Workspace, all four GUI widgets, and SearchResultModel. It handles button signals, file loading, selections, state changes, and screen updates. I plan to add a CorpusBuildJob helper using C++ standard-library threading. Its worker thread receives a snapshot of the workspace, constructs a separate ProcessingCore, and runs rebuild() with the configured per-document delay. A GUI-thread QTimer checks for completion. The worker never accesses Qt widgets or the live workspace.

## 2. Corpus states and transitions

I will use NotBuilt, Building, Current, and Failed, plus a workspace revision number. Build Corpus starts a job tagged with the current revision and enters Building. Successful completion enters Current only when the job's revision still matches the workspace revision; otherwise the result is discarded. Failure enters Failed and displays an error. Adding a document or Reset increments the revision, invalidates the old corpus, and clears dependent views. Search, term inspection, and context are available only in Current.

## 3. Risky event sequences (R1–R4)

Sequence A — add during build (R1/R2): A build starts for revision 2. The user adds another document while the worker processes its private snapshot. The GUI immediately updates the document list, advances to revision 3, and invalidates search-dependent displays. When the old build finishes, the revision check rejects it; its chunk count is never presented as current. The user can build the new workspace afterward.

Sequence B — Reset during build (R2/R4): A build starts, then the user clicks Reset. The GUI clears documents and results and increments the revision. The worker may finish later, but its result is obsolete and cannot restore the cleared corpus. Its thread remains owned and is joined before destruction.

For R1, expensive rebuilding and artificial delays run only in the worker. For R3, worker exceptions become a completion error shown by MainWindow, which leaves Building. For R4, I will limit outstanding jobs, collect completed workers, and test Exit during delayed builds. Joining a still-running job can delay shutdown, so I will investigate cooperative cancellation rather than block the GUI unnecessarily.

## 4. Ownership of a built corpus

The worker exclusively owns its temporary ProcessingCore using std::unique_ptr. On completion, ownership moves to the GUI thread. If the revision is valid, MainWindow replaces its previous current core; otherwise the temporary core is destroyed. Reset releases the current core. The worker and GUI do not concurrently mutate the same Workspace or ProcessingCore.

## 5. Rejected alternative

I rejected rebuilding on the GUI thread while repeatedly calling QApplication::processEvents(). It risks reentrant event handling, still occupies the GUI thread, and violates the specification's threading constraint. A separate build snapshot makes validity checks and ownership clearer.