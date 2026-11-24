#pragma once

#include "aiws/processing_core.hpp"

#include <QObject>
#include <atomic>
#include <condition_variable>
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

    std::optional<Request> pending_;
    std::optional<Result> result_;

    std::shared_ptr<std::atomic<bool>> active_;

    std::atomic<bool> stopping_{false};

    std::thread worker_;
};

}  // namespace aiws::gui