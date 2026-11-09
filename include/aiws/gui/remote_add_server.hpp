#pragma once

#include <QObject>
#include <QString>
#include <cstddef>

#include <atomic>
#include <string>
#include <thread>

namespace aiws::gui {

class RemoteAddServer final : public QObject {
    Q_OBJECT

public:
    explicit RemoteAddServer(const QString& socket_path);
    ~RemoteAddServer() override;

    RemoteAddServer(const RemoteAddServer&) = delete;
    RemoteAddServer& operator=(const RemoteAddServer&) = delete;

    bool start();
    void stop() noexcept;

signals:
    void document_received(const QString& path);

private:
    void run() noexcept;
    void handle_client(int client_fd) noexcept;
    bool receive_exact(int fd, void* buffer, std::size_t size) noexcept;

    std::string socket_path_;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
    int listen_fd_{-1};
    bool owns_socket_{false};
};

}  // namespace aiws::gui
