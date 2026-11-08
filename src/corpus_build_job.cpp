#include "aiws/gui/corpus_build_job.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace aiws::gui {

CorpusBuildJob::CorpusBuildJob(
    Workspace snapshot,
    std::size_t revision,
    int delay_ms)
    : revision_(revision),
      worker_(&CorpusBuildJob::run,
              this,
              std::move(snapshot),
              delay_ms) {}

CorpusBuildJob::~CorpusBuildJob() {
    request_cancel();
    join();
}

bool CorpusBuildJob::finished() const noexcept {
    return finished_.load(std::memory_order_acquire);
}

void CorpusBuildJob::request_cancel() noexcept {
    cancel_.store(true, std::memory_order_relaxed);
}

void CorpusBuildJob::join() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::size_t CorpusBuildJob::revision() const noexcept {
    return revision_;
}

std::unique_ptr<ProcessingCore> CorpusBuildJob::take_result() {
    // Only call after the worker has finished and been joined.
    return std::move(result_);
}

const std::string& CorpusBuildJob::error() const noexcept {
    return error_;
}

void CorpusBuildJob::run(Workspace snapshot,
                         int delay_ms) noexcept {
    try {
        // Simulate additional work for every document.
        // This delay runs on the worker, not the GUI.
        for (std::size_t i = 0;
             i < snapshot.documents().size();
             ++i) {

            int remaining = std::max(0, delay_ms);

            while (remaining > 0 &&
                   !cancel_.load(std::memory_order_relaxed)) {

                const int slice = std::min(remaining, 10);

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(slice));

                remaining -= slice;
            }

            if (cancel_.load(std::memory_order_relaxed)) {
                break;
            }
        }

        if (!cancel_.load(std::memory_order_relaxed)) {
            auto next = std::make_unique<ProcessingCore>();
            next->rebuild(snapshot);

            if (!cancel_.load(std::memory_order_relaxed)) {
                result_ = std::move(next);
            }
        }

    } catch (const std::exception& ex) {
        error_ = ex.what();

    } catch (...) {
        error_ = "Unknown corpus build error";
    }

    finished_.store(true, std::memory_order_release);
}

}  // namespace aiws::gui
