#pragma once

#include "aiws/processing_core.hpp"

#include <QObject>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace aiws::gui {

class CorpusBuildJob final : public QObject {
    Q_OBJECT

public:
    struct Result {
        std::size_t revision;
        std::unique_ptr<ProcessingCore> core;
        std::string error;
    };

    CorpusBuildJob();
    ~CorpusBuildJob() override;

    void submit(Workspace snapshot,
                std::size_t revision,
                int delay_ms);

    void request_cancel();

    std::optional<Result> take_result();

    // Bounded lifecycle observation for synchronization tests.
    bool wait_until_active(std::chrono::milliseconds timeout);
    bool wait_until_idle(std::chrono::milliseconds timeout);

signals:
    void completed();

private:
    struct Request {
        Workspace snapshot;
        std::size_t revision;
        int delay_ms;
        std::shared_ptr<std::atomic<bool>> cancel;
    };

    void run();

    std::mutex mutex_;
    std::condition_variable condition_;

    // Separate condition variable for lifecycle observers.
    std::condition_variable lifecycle_condition_;

    std::optional<Request> pending_;
    std::optional<Result> result_;

    std::shared_ptr<std::atomic<bool>> active_;

    // Protected by mutex_.
    bool idle_{false};

    std::atomic<bool> stopping_{false};

    std::thread worker_;
};

}  // namespace aiws::gui