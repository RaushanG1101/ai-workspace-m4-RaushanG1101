
#pragma once

#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>

namespace aiws::gui {

class CorpusBuildJob final {
public:
    CorpusBuildJob(Workspace snapshot,
                   std::size_t revision,
                   int delay_ms);
    ~CorpusBuildJob();

    CorpusBuildJob(const CorpusBuildJob&) = delete;
    CorpusBuildJob& operator=(const CorpusBuildJob&) = delete;

    bool finished() const noexcept;
    void request_cancel() noexcept;
    void join();

    std::size_t revision() const noexcept;
    std::unique_ptr<ProcessingCore> take_result();
    const std::string& error() const noexcept;

private:
    void run(Workspace snapshot, int delay_ms) noexcept;

    std::size_t revision_;
    std::atomic<bool> finished_{false};
    std::atomic<bool> cancel_{false};
    std::thread worker_;
    std::unique_ptr<ProcessingCore> result_;
    std::string error_;
};

}  // namespace aiws::gui
