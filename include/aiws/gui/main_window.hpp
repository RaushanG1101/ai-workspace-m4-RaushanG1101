#pragma once

#include <QMainWindow>

#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"

#include <cstddef>
#include <memory>

class QLabel;
class QPushButton;
class QString;
class QWidget;

namespace aiws::gui {

class ContextWidget;
class CorpusBuildJob;
class RemoteAddServer;
class SearchWidget;
class TermInspectorWidget;
class WorkspaceWidget;

/**
 * @brief Main application window and coordinator for AI Workspace.
 *
 * Owns the live Workspace, current ProcessingCore, persistent build
 * worker, and corpus lifecycle State objects.
 */
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    using ExitCallback = void (*)(QWidget* parent);

    explicit MainWindow(QWidget* parent = nullptr);

    ~MainWindow() override;

    void set_exit_callback(ExitCallback callback) noexcept;

    bool add_document_file(const QString& path);

    void set_build_delay_ms(int ms_per_document);

    bool start_ipc_server(const QString& socket_path);

private slots:
    void add_document();

    void build_corpus();

    void check_build_completion();

    void add_document_from_ipc(const QString& path);

    void document_selected(int row);

    void search_requested(const QString& query, int k);

    void query_changed(const QString& query);

    void build_context_requested(int token_budget);

    void source_document_requested(
        const QString& document_id);

    void term_inspection_requested(
        const QString& term,
        const QString& chunk_id);

    void reset_application();

    void exit_application();

private:
    // Polymorphic corpus lifecycle states.
    class State;
    class EmptyState;
    class DirtyState;
    class BuildingState;
    class ReadyState;

    void start_build();

    void cancel_build();

    void accept_build(
        std::unique_ptr<ProcessingCore> core);

    void report_build_failure(const QString& error);

    void update_action_states();

    void invalidate_corpus();

    void clear_processing_outputs();

    // GUI-thread-owned application data.
    Workspace workspace_;
    ProcessingCore processing_core_;

    std::unique_ptr<State> state_;

    std::size_t workspace_revision_{0};
    int build_delay_ms_{0};

    // Persistent background worker.
    std::unique_ptr<CorpusBuildJob> build_job_;

    std::unique_ptr<RemoteAddServer> remote_add_server_;

    ExitCallback exit_callback_{nullptr};

    WorkspaceWidget* workspace_widget_{nullptr};
    SearchWidget* search_widget_{nullptr};
    TermInspectorWidget* term_inspector_widget_{nullptr};
    ContextWidget* context_widget_{nullptr};

    QLabel* status_label_{nullptr};

    QPushButton* reset_button_{nullptr};
    QPushButton* exit_button_{nullptr};
};

}  // namespace aiws::gui