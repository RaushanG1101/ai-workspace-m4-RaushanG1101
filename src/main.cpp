#include "aiws/gui/main_window.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QString>
#include <QWidget>

#include <cstdlib>
#include <string>
#include <unistd.h>

namespace {

void confirm_exit(QWidget* parent) {
    if (QMessageBox::question(parent, "Exit", "Exit AI Workspace?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
        QApplication::quit();
    }
}

QString default_socket_path() {
    const char* user = std::getenv("USER");

    if (user != nullptr && user[0] != '\0') {
        return "/tmp/aiws-" + QString::fromLocal8Bit(user) + ".sock";
    }

    return "/tmp/aiws-" +
        QString::number(static_cast<unsigned int>(::getuid())) + ".sock";
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    aiws::gui::MainWindow window;

    QString socket_path = default_socket_path();

    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);

        if (argument == "--build-delay-ms") {
            if (i + 1 >= argc) {
                return 2;
            }

            bool ok = false;
            const int delay =
                QString::fromLocal8Bit(argv[++i]).toInt(&ok);

            if (!ok || delay < 0) {
                return 2;
            }

            window.set_build_delay_ms(delay);
        } else if (argument == "--ipc-socket") {
            if (i + 1 >= argc) {
                return 2;
            }

            socket_path = QString::fromLocal8Bit(argv[++i]);

            if (socket_path.isEmpty()) {
                return 2;
            }
        } else {
            return 2;
        }
    }

    if (!window.start_ipc_server(socket_path)) {
        return 2;
    }

    window.set_exit_callback(&confirm_exit);
    window.show();

    return app.exec();
}
