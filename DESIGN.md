# M4 Decision Record

See specification Section 8.

1. Threads, ownership, and shared data
MainWindow runs on the Qt GUI thread and owns the live Workspace, ProcessingCore, CorpusBuildJob, and RemoteAddServer. Qt parent ownership manages widgets and the completion QTimer; std::unique_ptr manages worker objects. CorpusBuildJob runs a std::thread over a private workspace snapshot and builds a separate ProcessingCore. It never touches GUI widgets or the live workspace. The build covers the whole workspace, not just the selected document.

Cross-thread mutable state consists of the atomic cancellation and completion flags and the worker-written result/error. The worker publishes completion after writing its result/error; the GUI observes completion and joins the worker before accessing those values. The snapshot is private to the worker. RemoteAddServer uses a separate std::thread for POSIX socket I/O and emits document_received through a queued Qt connection; MainWindow::add_document_from_ipc() runs on the GUI thread. The server acknowledges requests without waiting for GUI processing. Workers are stopped and joined during destruction.

2. Corpus validity and lifecycle
workspace_revision_ increments on Add Document, Remote Add, and Reset. Each build records its starting revision. MainWindow::check_build_completion() accepts a successful result only when its revision matches the live workspace; stale results are discarded. Search, Term Inspector, and Build Context stay disabled until the corpus is current. Build Corpus is disabled while a job is outstanding rather than starting another concurrent build. --build-delay-ms delays each document in the worker, not the GUI. Reset invalidates the corpus and requests cancellation; failures report an error and restore the nonbuilding state. Exit stops IPC and joins workers.

3. Changes since checkpoint
I added CorpusBuildJob, cooperative cancellation, and QTimer completion polling to implement responsive building and safe shutdown. CR-1 required a socket server, command-line sender, and integration tests. The M3 corpus-processing algorithms were retained.

4. CR-1 impact and protocol
main.cpp parses --ipc-socket; MainWindow::start_ipc_server() connects the server's queued document_received signal to add_document_from_ipc(), which calls add_document_file() to reuse normal document insertion and corpus invalidation. src/remote_add_server.cpp handles server I/O; tools/aiws_send.cpp handles the client; CMake adds the sender target. The request is a four-byte unsigned network-order UTF-8 path length followed by exactly that many UTF-8 bytes, without a terminator. Reads handle fragmentation; the one-byte acknowledgment does not wait for the GUI. Sender exit codes are 0 (accepted), 1 (rejected), and 2 (communication failure).

5. Fault injection and testing
Both production switches are in MainWindow::check_build_completion(): AIWS_FAULT_ACCEPT_STALE bypasses revision validation and is caught by completed_stale_build_is_rejected; AIWS_FAULT_LOSE_FAILURE suppresses failure handling and is caught by failure_and_recovery. Neither switch is referenced in tests or CMake. Normal Docker tests passed; both fault-enabled suites failed as intended within 60 seconds. Tests also cover responsiveness, Reset, destruction, IPC filenames, fragmented requests, and socket cleanup.

6. AI-use disclosure
I used ChatGPT to interpret requirements, draft and review C++/Qt code and tests, troubleshoot CMake/Docker, and organize documentation. I reviewed the code and ran the Docker tests.

# Pair programming log
Partners: Vanesha Hari and Raushan Goureddy
Session 1: Responsive Corpus Building
Date: Sunday, November 8, 2026
Start: 2:30 PM
End: 3:00 PM
Codebase: Vanesha Hari's M4 project
Focus: Background corpus building, generation checks, and delayed-build tests

Work Completed
This session focused on moving corpus building off the main Qt thread so the GUI could stay responsive. We looked at the std::thread build path, the --build-delay-ms option, and the generation counter used to detect when the workspace changes during a build. We also looked at how the worker reports its result back to the main Qt thread and what should happen during Reset or Exit.

Driver and Navigator Roles
2:30 PM to 2:45 PM
- Driver: Vanesha Hari
- Navigator: Raushan Goureddy
Vanesha worked through the background-build and generation-check logic. Raushan's navigator contribution was identifying the need to handle a document being added before the worker finishes, so an old result cannot become current.

2:45 PM to 3:00 PM
- Driver: Raushan Goureddy
- Navigator: Vanesha Hari
Raushan went through delayed-build and Reset testing. Vanesha's navigator contribution was checking the expected interface state, especially that Search, Inspect, and Build Context remain disabled when the workspace has changed.

Role swap: 2:45 PM.
Testing and Challenges
The relevant tests cover normal builds, simulated slow builds, adding documents during a build, Reset during a build, and failure reporting. The tricky part was that moving work to a thread prevents freezing but does not automatically keep the corpus current. Generation checks handle that problem.

Session 2: Remote Add Document IPC
Date: Sunday, November 8, 2026
Start: 6:10 PM
End: 6:40 PM
Codebase: Raushan Goureddy's M4 project
Focus: Unix-domain socket requests, message framing, and workspace integration

Work Completed
This session focused on allowing aiws_send to send a document path to the running application. We reviewed how the server reads a four-byte length and the UTF-8 path, handles a request sent in multiple pieces, checks file readability, and passes the accepted path to the GUI. We also looked at how Remote Add should invalidate a build in progress just like the Add Document button.

Driver and Navigator Roles
6:10 PM to 6:25 PM
- Driver: Raushan Goureddy
- Navigator: Vanesha Hari
Raushan worked through the socket server and client communication. Vanesha's navigator contribution was calling out the need to test partial socket reads rather than assuming that the entire request arrives in one operation.

6:25 PM to 6:40 PM
- Driver: Vanesha Hari
- Navigator: Raushan Goureddy
Vanesha went through the workspace integration and expected behavior when a document arrives during a build. Raushan's navigator contribution was reviewing invalid-path handling and whether the server socket is removed on Exit.

Role swap: 6:25 PM.
Testing and Challenges
The Remote Add tests cover valid and invalid document paths, spaces and Unicode in filenames, newlines, split requests, cleanup, and the unavailable-server exit code. During Docker testing, the default socket was /tmp/aiws-0.sock because the container ran as UID 0 without USER set. Using the correct path allowed the client to connect.

Reflection
Working through the code together was most helpful when the driver explained the current change and the navigator suggested a concrete edge case to test. On the background-build side, we focused on keeping results consistent after workspace changes. On the socket side, we focused on correctly reading requests and getting them into the Qt GUI without blocking it. Switching roles made it easier to see both the implementation details and the cases that could go wrong.