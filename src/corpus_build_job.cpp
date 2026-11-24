#include "aiws/gui/corpus_build_job.hpp"

#include <chrono>
#include <exception>
#include <utility>

namespace aiws::gui {

CorpusBuildJob::CorpusBuildJob()
    : worker_(&CorpusBuildJob::run, this) {}

CorpusBuildJob::~CorpusBuildJob() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        stopping_.store(true);
        pending_.reset();
    }

#ifndef AIWS_FAULT_SILENT_SHUTDOWN
    condition_.notify_one();
#endif

    worker_.join();
}

void CorpusBuildJob::submit(
    Workspace snapshot,
    std::size_t revision,
    int delay_ms) {

    auto cancel =
        std::make_shared<std::atomic<bool>>(false);

    {
        std::lock_guard<std::mutex> lock(mutex_);

        pending_ = Request{
            std::move(snapshot),
            revision,
            delay_ms,
            std::move(cancel)
        };
    }

    condition_.notify_one();
}

void CorpusBuildJob::request_cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        pending_.reset();
        result_.reset();

        if (active_) {
            active_->store(true);
        }
    }

    condition_.notify_one();
}

std::optional<CorpusBuildJob::Result>
CorpusBuildJob::take_result() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto result = std::move(result_);
    result_.reset();

    return result;
}

bool CorpusBuildJob::wait_until_active(
    std::chrono::milliseconds timeout) {

    std::unique_lock<std::mutex> lock(mutex_);

    return lifecycle_condition_.wait_for(
        lock,
        timeout,
        [this] {
            return active_ != nullptr;
        });
}

bool CorpusBuildJob::wait_until_idle(
    std::chrono::milliseconds timeout) {

    std::unique_lock<std::mutex> lock(mutex_);

    return lifecycle_condition_.wait_for(
        lock,
        timeout,
        [this] {
            return idle_ &&
                   !pending_.has_value() &&
                   !active_;
        });
}

void CorpusBuildJob::run() {
    for (;;) {
        std::unique_lock<std::mutex> lock(mutex_);

        idle_ = true;
        lifecycle_condition_.notify_all();

        condition_.wait(lock, [this] {
            return stopping_.load() ||
                   pending_.has_value();
        });

        idle_ = false;

        if (stopping_.load()) {
            return;
        }

        auto request = std::move(*pending_);
        pending_.reset();

        active_ = request.cancel;

        lifecycle_condition_.notify_all();

        lock.unlock();

        auto cancelled = [&] {
#ifdef AIWS_FAULT_NO_CANCEL
            return stopping_.load();
#else
            return stopping_.load() ||
                   request.cancel->load();
#endif
        };

        Result result{
            request.revision,
            nullptr,
            {}
        };

        try {
            auto core =
                std::make_unique<ProcessingCore>();

            if (core->rebuild_interruptible(
                    request.snapshot,
                    [&] {
                        if (cancelled()) {
                            return false;
                        }

                        // Simulated document processing delay.
                        // Cancellation and shutdown can interrupt it.
                        std::unique_lock<std::mutex>
                            delay_lock(mutex_);

                        condition_.wait_for(
                            delay_lock,
                            std::chrono::milliseconds(
                                request.delay_ms),
                            cancelled);

                        return !cancelled();
                    },
                    [&] {
                        return !cancelled();
                    })) {

                result.core = std::move(core);
            }

        } catch (const std::exception& ex) {
            result.error = ex.what();

        } catch (...) {
            result.error =
                "Unknown corpus build error";
        }

        lock.lock();

        active_.reset();

        const bool publish =
            !stopping_.load() &&
            !request.cancel->load();

        if (publish) {
            result_ = std::move(result);
        }

        lock.unlock();

        if (publish) {
            emit completed();
        }
    }
}

}  // namespace aiws::gui