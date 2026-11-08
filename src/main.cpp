#include "aiws/gui/main_window.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QString>
#include <QWidget>

void confirm_exit(QWidget* parent) {
    const QMessageBox::StandardButton answer =
        QMessageBox::question(
            parent,
            "Confirm Exit",
            "Are you sure you want to exit?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );

    if (answer == QMessageBox::Yes) {
        QApplication::quit();
    }
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    aiws::gui::MainWindow window;

    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) ==
                "--build-delay-ms" &&
            i + 1 < argc) {

            bool ok = false;

            const int delay =
                QString::fromLocal8Bit(argv[++i]).toInt(&ok);

            if (!ok || delay < 0) {
                return 2;
            }

            window.set_build_delay_ms(delay);
        }
    }

    window.set_exit_callback(&confirm_exit);
    window.show();

    return app.exec();
}
