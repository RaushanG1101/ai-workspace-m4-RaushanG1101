// Your M4 tests (specification, Section 6).
#include "aiws/gui/main_window.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QTest>

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

        QTRY_VERIFY_WITH_TIMEOUT(
            !has_status(window, "Building"), 3000);

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
};

QTEST_MAIN(StudentTests)

#include "student_tests.moc"
