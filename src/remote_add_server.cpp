
#include "aiws/gui/remote_add_server.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>

#include <vector>

namespace aiws::gui {

namespace {

constexpr std::uint32_t max_path_bytes = 1024 * 1024;

bool send_reply(int fd, unsigned char reply) {
    return ::send(fd, &reply, 1, MSG_NOSIGNAL) == 1;
}

bool readable_file(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    struct stat info {};
    const bool valid =
        ::fstat(fd, &info) == 0 && S_ISREG(info.st_mode);

    ::close(fd);
    return valid;
}

}  // namespace

RemoteAddServer::RemoteAddServer(const QString& socket_path)
    : socket_path_(socket_path.toUtf8().toStdString()) {}

RemoteAddServer::~RemoteAddServer() {
    stop();
}

bool RemoteAddServer::start() {
    if (worker_.joinable() || listen_fd_ >= 0 ||
        socket_path_.empty()) {
        return false;
    }

    sockaddr_un address {};
    address.sun_family = AF_UNIX;

    if (socket_path_.size() >= sizeof(address.sun_path)) {
        return false;
    }

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        return false;
    }

    std::memcpy(address.sun_path, socket_path_.c_str(),
                socket_path_.size() + 1);

    // Do not remove another running application's socket.
    if (::bind(listen_fd_,
               reinterpret_cast<sockaddr*>(&address),
               sizeof(address)) != 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    owns_socket_ = true;

    if (::listen(listen_fd_, 8) != 0) {
        stop();
        return false;
    }

    stopping_.store(false, std::memory_order_relaxed);

    try {
        worker_ = std::thread(&RemoteAddServer::run, this);
    } catch (...) {
        stop();
        return false;
    }

    return true;
}

void RemoteAddServer::stop() noexcept {
    stopping_.store(true, std::memory_order_relaxed);

    if (worker_.joinable()) {
        worker_.join();
    }

    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (owns_socket_) {
        ::unlink(socket_path_.c_str());
        owns_socket_ = false;
    }
}

bool RemoteAddServer::receive_exact(
    int fd, void* buffer, std::size_t size) noexcept {

    auto* bytes = static_cast<unsigned char*>(buffer);
    std::size_t received = 0;

    while (received < size &&
           !stopping_.load(std::memory_order_relaxed)) {

        pollfd descriptor {};
        descriptor.fd = fd;
        descriptor.events = POLLIN;

        const int ready = ::poll(&descriptor, 1, 50);

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (ready == 0) {
            continue;
        }

        if (descriptor.revents & (POLLERR | POLLNVAL)) {
            return false;
        }

        const ssize_t count =
            ::recv(fd, bytes + received, size - received, 0);

        if (count <= 0) {
            return false;
        }

        received += static_cast<std::size_t>(count);
    }

    return received == size;
}

void RemoteAddServer::handle_client(int client_fd) noexcept {
    std::uint32_t network_length = 0;

    if (!receive_exact(client_fd,
                       &network_length,
                       sizeof(network_length))) {
        return;
    }

    const std::uint32_t length = ntohl(network_length);

    if (length == 0 || length > max_path_bytes) {
        send_reply(client_fd, 1);
        return;
    }

    std::vector<char> bytes(length);

    if (!receive_exact(client_fd, bytes.data(), bytes.size())) {
        return;
    }

    const std::string path(bytes.begin(), bytes.end());

    if (path.find('\0') != std::string::npos ||
        !readable_file(path)) {
        send_reply(client_fd, 1);
        return;
    }

    emit document_received(
        QString::fromUtf8(bytes.data(), static_cast<int>(bytes.size())));

    send_reply(client_fd, 0);
}

void RemoteAddServer::run() noexcept {
    while (!stopping_.load(std::memory_order_relaxed)) {
        pollfd descriptor {};
        descriptor.fd = listen_fd_;
        descriptor.events = POLLIN;

        const int ready = ::poll(&descriptor, 1, 50);

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (ready == 0 || !(descriptor.revents & POLLIN)) {
            continue;
        }

        const int client_fd =
            ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);

        if (client_fd < 0) {
            continue;
        }

        handle_client(client_fd);
        ::close(client_fd);
    }
}

}  // namespace aiws::gui
