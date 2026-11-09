
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

using Clock = std::chrono::steady_clock;

std::string default_socket_path() {
    const char* user = std::getenv("USER");

    if (user != nullptr && user[0] != '\0') {
        return "/tmp/aiws-" + std::string(user) + ".sock";
    }

    return "/tmp/aiws-" + std::to_string(::getuid()) + ".sock";
}

int remaining_ms(Clock::time_point deadline) {
    const auto now = Clock::now();

    if (now >= deadline) {
        return 0;
    }

    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now).count());
}

bool wait_for(int fd, short events, Clock::time_point deadline) {
    while (true) {
        const int timeout = remaining_ms(deadline);

        if (timeout == 0) {
            return false;
        }

        pollfd descriptor {};
        descriptor.fd = fd;
        descriptor.events = events;

        const int result = ::poll(&descriptor, 1, timeout);

        if (result < 0 && errno == EINTR) {
            continue;
        }

        if (result <= 0) {
            return false;
        }

        if (descriptor.revents & (POLLERR | POLLNVAL)) {
            return false;
        }

        return (descriptor.revents & (events | POLLHUP)) != 0;
    }
}

bool send_all(int fd, const void* data, std::size_t size,
              Clock::time_point deadline) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::size_t sent = 0;

    while (sent < size) {
        if (!wait_for(fd, POLLOUT, deadline)) {
            return false;
        }

        const ssize_t count =
            ::send(fd, bytes + sent, size - sent,
                   MSG_NOSIGNAL | MSG_DONTWAIT);

        if (count < 0 && (errno == EAGAIN || errno == EINTR)) {
            continue;
        }

        if (count <= 0) {
            return false;
        }

        sent += static_cast<std::size_t>(count);
    }

    return true;
}

bool receive_reply(int fd, unsigned char& reply,
                   Clock::time_point deadline) {
    while (true) {
        if (!wait_for(fd, POLLIN, deadline)) {
            return false;
        }

        const ssize_t count =
            ::recv(fd, &reply, 1, MSG_DONTWAIT);

        if (count < 0 && (errno == EAGAIN || errno == EINTR)) {
            continue;
        }

        return count == 1;
    }
}

bool connect_with_timeout(int fd, const sockaddr_un& address) {
    const int flags = ::fcntl(fd, F_GETFL, 0);

    if (flags < 0 ||
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return false;
    }

    const int result = ::connect(
        fd, reinterpret_cast<const sockaddr*>(&address),
        sizeof(address));

    if (result == 0) {
        return true;
    }

    if (errno != EINPROGRESS) {
        return false;
    }

    const auto deadline =
        Clock::now() + std::chrono::seconds(2);

    if (!wait_for(fd, POLLOUT, deadline)) {
        return false;
    }

    int socket_error = 0;
    socklen_t length = sizeof(socket_error);

    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR,
                     &socket_error, &length) != 0) {
        return false;
    }

    return socket_error == 0;
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

int main(int argc, char* argv[]) {
    std::string socket_path = default_socket_path();
    int index = 1;

    if (index < argc &&
        std::string(argv[index]) == "--socket") {
        if (index + 1 >= argc) {
            return 1;
        }

        socket_path = argv[index + 1];
        index += 2;
    }

    if (argc - index != 2 ||
        std::string(argv[index]) != "add") {
        std::cerr << "Usage: aiws_send [--socket PATH] add FILE\n";
        return 1;
    }

    const std::filesystem::path input_path(argv[index + 1]);

    std::error_code error;
    const std::filesystem::path absolute_path =
        std::filesystem::absolute(input_path, error);

    if (error) {
        std::cerr << "Invalid file path.\n";
        return 1;
    }

    const std::string file_path = absolute_path.string();

    if (!readable_file(file_path) ||
        file_path.empty() ||
        file_path.size() > 1024 * 1024) {
        std::cerr << "File is unreadable or path is invalid.\n";
        return 1;
    }

    sockaddr_un address {};
    address.sun_family = AF_UNIX;

    if (socket_path.empty() ||
        socket_path.size() >= sizeof(address.sun_path)) {
        std::cerr << "Invalid socket path.\n";
        return 2;
    }

    std::memcpy(address.sun_path, socket_path.c_str(),
                socket_path.size() + 1);

    const int fd =
        ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (fd < 0) {
        std::cerr << "Could not create socket.\n";
        return 2;
    }

    if (!connect_with_timeout(fd, address)) {
        std::cerr << "Could not connect to AI Workspace.\n";
        ::close(fd);
        return 2;
    }

    const auto deadline =
        Clock::now() + std::chrono::seconds(5);

    const std::uint32_t length =
        htonl(static_cast<std::uint32_t>(file_path.size()));

    unsigned char reply = 1;

    const bool sent =
        send_all(fd, &length, sizeof(length), deadline) &&
        send_all(fd, file_path.data(), file_path.size(), deadline);

    const bool received =
        sent && receive_reply(fd, reply, deadline);

    ::close(fd);

    if (!received) {
        std::cerr << "Communication with AI Workspace failed.\n";
        return 2;
    }

    if (reply != 0) {
        std::cerr << "AI Workspace rejected the document.\n";
        return 1;
    }

    std::cout << "Document accepted: " << file_path << '\n';
    return 0;
}
