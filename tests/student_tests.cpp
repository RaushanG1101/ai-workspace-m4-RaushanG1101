
// Your M4 tests (specification, Section 6).
#include "aiws/gui/main_window.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QMetaObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <memory>

using aiws::gui::MainWindow;

class StudentTests : public QObject {
    Q_OBJECT

private:
    QString create_file(const QString& directory,
                        const QString& name,
                        const QString& text) {
        const QString path = directory + "/" + name;

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return QString();
        }

        file.write(text.toUtf8());
        file.close();

        return path;
    }

    bool has_status(MainWindow& window, const QString& text) {
        const auto labels = window.findChildren<QLabel*>();

        for (QLabel* label : labels) {
            if (label->text().contains(text)) {
                return true;
            }
        }

        return false;
    }

    bool start_build(MainWindow& window) {
        return QMetaObject::invokeMethod(
            &window, "build_corpus", Qt::DirectConnection);
    }

    bool reset_window(MainWindow& window) {
        return QMetaObject::invokeMethod(
            &window, "reset_application", Qt::DirectConnection);
    }

    int run_sender(const QString& socket_path,
                   const QString& file_path) {
        const QString sender =
            QCoreApplication::applicationDirPath() + "/aiws_send";

        QProcess process;
        process.start(sender, {
            "--socket", socket_path,
            "add", file_path
        });

        if (!process.waitForStarted(3000)) {
            return -1;
        }

        if (!process.waitForFinished(5000)) {
            process.kill();
            process.waitForFinished();
            return -1;
        }

        if (process.exitStatus() != QProcess::NormalExit) {
            return -1;
        }

        return process.exitCode();
    }

private slots:
    void build_completes() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = create_file(
            directory.path(), "ai.txt",
            "Artificial intelligence uses machine learning.");

        QVERIFY(!path.isEmpty());

        MainWindow window;
        window.set_build_delay_ms(200);

        QVERIFY(window.add_document_file(path));
        QVERIFY(start_build(window));

        QVERIFY(has_status(window, "Building"));

        QTRY_VERIFY_WITH_TIMEOUT(
            has_status(window, "Corpus built."), 3000);

        QVERIFY(has_status(window, "chunks"));
    }

    void gui_remains_responsive() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString first = create_file(
            directory.path(), "first.txt",
            "The first document discusses threads.");

        const QString second = create_file(
            directory.path(), "second.txt",
            "The second document discusses synchronization.");

        QVERIFY(!first.isEmpty());
        QVERIFY(!second.isEmpty());

        MainWindow window;
        window.set_build_delay_ms(500);

        QVERIFY(window.add_document_file(first));
        QVERIFY(start_build(window));

        QElapsedTimer timer;
        timer.start();

        QVERIFY(window.add_document_file(second));

        QVERIFY(timer.elapsed() < 400);
        QVERIFY(has_status(window, "Document added."));

        QTest::qWait(700);

        QVERIFY(!has_status(window, "Corpus built."));
    }

    void reset_discards_old_build() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = create_file(
            directory.path(), "reset.txt",
            "This document should not survive Reset.");

        QVERIFY(!path.isEmpty());

        MainWindow window;
        window.set_build_delay_ms(500);

        QVERIFY(window.add_document_file(path));
        QVERIFY(start_build(window));
        QVERIFY(reset_window(window));

        QVERIFY(has_status(window, "Application reset."));

        QTest::qWait(700);

        QVERIFY(!has_status(window, "Corpus built."));
        QVERIFY(!has_status(window, "chunks"));

        QVERIFY(window.add_document_file(path));
        QVERIFY(start_build(window));

        QTRY_VERIFY_WITH_TIMEOUT(
            has_status(window, "Corpus built."), 3000);
    }

    void failure_and_recovery() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = create_file(
            directory.path(), "duplicate.txt",
            "A document with a repeated identifier.");

        QVERIFY(!path.isEmpty());

        MainWindow window;

        QVERIFY(window.add_document_file(path));
        QVERIFY(window.add_document_file(path));

        QVERIFY(start_build(window));

        QTRY_VERIFY_WITH_TIMEOUT(
            has_status(window, "Build failed:"), 3000);

        QVERIFY(!has_status(window, "Corpus built."));

        QVERIFY(reset_window(window));

        QVERIFY(window.add_document_file(path));
        QVERIFY(start_build(window));

        QTRY_VERIFY_WITH_TIMEOUT(
            has_status(window, "Corpus built."), 3000);
    }

    void destruction_during_build() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = create_file(
            directory.path(), "exit.txt",
            "Testing worker cleanup during application exit.");

        QVERIFY(!path.isEmpty());

        QElapsedTimer timer;
        timer.start();

        {
            MainWindow window;
            window.set_build_delay_ms(2000);

            QVERIFY(window.add_document_file(path));
            QVERIFY(start_build(window));
        }

        QVERIFY(timer.elapsed() < 1000);
    }

    void completed_stale_build_is_rejected() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString first = create_file(
            directory.path(),
            "first.txt",
            "The original document discusses artificial intelligence.");

        const QString second = create_file(
            directory.path(),
            "second.txt",
            "The new document discusses thread synchronization.");

        QVERIFY(!first.isEmpty());
        QVERIFY(!second.isEmpty());

        MainWindow window;
        window.set_build_delay_ms(0);

        QVERIFY(window.add_document_file(first));
        QVERIFY(start_build(window));

        // Let the worker finish without processing GUI events.
        QTest::qSleep(1500);

        // Change the workspace before processing completion.
        QVERIFY(window.add_document_file(second));

        // Process the completion timer.
        QTest::qWait(300);

        // The completed old corpus must not be accepted.
        QVERIFY(!has_status(window, "Corpus built."));
        QVERIFY(!has_status(window, "chunks"));
    }

    void ipc_remote_add_during_build() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString socket_path =
            directory.path() + "/remote.sock";

        const QString first = create_file(
            directory.path(), "first.txt",
            "Artificial intelligence uses machine learning.");

        const QString second = create_file(
            directory.path(), "second.txt",
            "Threads can execute work concurrently.");

        QVERIFY(!first.isEmpty());
        QVERIFY(!second.isEmpty());

        MainWindow window;
        window.set_build_delay_ms(500);

        QVERIFY(window.start_ipc_server(socket_path));
        QVERIFY(window.add_document_file(first));
        QVERIFY(start_build(window));

        QVERIFY(has_status(window, "Building"));

        QCOMPARE(run_sender(socket_path, second), 0);

        QTRY_VERIFY_WITH_TIMEOUT(
            has_status(window, "Document added."), 2000);

        // Allow the cancelled build to finish.
        QTest::qWait(800);

        QVERIFY(!has_status(window, "Corpus built."));

        // Rebuild with both documents.
        QVERIFY(start_build(window));

        QTRY_VERIFY_WITH_TIMEOUT(
            has_status(window, "Corpus built."), 3000);

        QVERIFY(has_status(window, "2 chunks"));
    }

    void ipc_special_filenames() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString socket_path =
            directory.path() + "/special.sock";

        const QString spaced = create_file(
            directory.path(), "file with spaces.txt",
            "Document with spaces in its filename.");

        const QString unicode = create_file(
            directory.path(), QString::fromUtf8("café-日本語.txt"),
            "Document with Unicode in its filename.");

        const QString newline = create_file(
            directory.path(), "file\nwith newline.txt",
            "Document with a newline in its filename.");

        QVERIFY(!spaced.isEmpty());
        QVERIFY(!unicode.isEmpty());
        QVERIFY(!newline.isEmpty());

        MainWindow window;
        QVERIFY(window.start_ipc_server(socket_path));

        QCOMPARE(run_sender(socket_path, spaced), 0);
        QCOMPARE(run_sender(socket_path, unicode), 0);
        QCOMPARE(run_sender(socket_path, newline), 0);

        // Process all queued remote-add requests before building.
        QTest::qWait(200);

        QVERIFY(start_build(window));

        QTRY_VERIFY_WITH_TIMEOUT(
            has_status(window, "Corpus built."), 3000);

        QVERIFY(has_status(window, "3 chunks"));
    }

    void ipc_fragmented_request() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString socket_path =
            directory.path() + "/fragmented.sock";

        const QString file_path = create_file(
            directory.path(), "fragmented.txt",
            "A document sent one byte at a time.");

        QVERIFY(!file_path.isEmpty());

        MainWindow window;
        QVERIFY(window.start_ipc_server(socket_path));

        const QByteArray socket_bytes = socket_path.toUtf8();
        const QByteArray path_bytes = file_path.toUtf8();

        QVERIFY(socket_bytes.size() <
                static_cast<int>(sizeof(sockaddr_un{}.sun_path)));

        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        QVERIFY(fd >= 0);

        sockaddr_un address{};
        address.sun_family = AF_UNIX;

        std::memcpy(
            address.sun_path,
            socket_bytes.constData(),
            static_cast<std::size_t>(socket_bytes.size()));

        QCOMPARE(
            ::connect(
                fd,
                reinterpret_cast<sockaddr*>(&address),
                sizeof(address)),
            0);

        const std::uint32_t length =
            htonl(static_cast<std::uint32_t>(path_bytes.size()));

        QByteArray message(
            reinterpret_cast<const char*>(&length),
            static_cast<int>(sizeof(length)));

        message.append(path_bytes);

        // Write the frame one byte at a time.
        for (char byte : message) {
            QCOMPARE(::send(fd, &byte, 1, 0), ssize_t(1));
            ::usleep(1000);
        }

        char reply = 1;

        QCOMPARE(::recv(fd, &reply, 1, 0), ssize_t(1));
        QCOMPARE(reply, char(0));

        ::close(fd);

        QTRY_VERIFY_WITH_TIMEOUT(
            has_status(window, "Document added."), 2000);

        QVERIFY(start_build(window));

        QTRY_VERIFY_WITH_TIMEOUT(
            has_status(window, "Corpus built."), 3000);

        QVERIFY(has_status(window, "1 chunks"));
    }

    void ipc_socket_cleanup_on_exit() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString socket_path =
            directory.path() + "/cleanup.sock";

        const QString file_path = create_file(
            directory.path(), "exit.txt",
            "Testing socket cleanup during application exit.");

        QVERIFY(!file_path.isEmpty());

        auto window = std::make_unique<MainWindow>();
        window->set_build_delay_ms(2000);

        QVERIFY(window->start_ipc_server(socket_path));
        QVERIFY(QFile::exists(socket_path));

        QVERIFY(window->add_document_file(file_path));
        QVERIFY(start_build(*window));

        QElapsedTimer timer;
        timer.start();

        // Destroy the window while a build is active.
        window.reset();

        QVERIFY(timer.elapsed() < 500);
        QVERIFY(!QFile::exists(socket_path));

        // Sender should report that the server is unavailable.
        QCOMPARE(run_sender(socket_path, file_path), 2);
    }
};

QTEST_MAIN(StudentTests)

#include "student_tests.moc"
