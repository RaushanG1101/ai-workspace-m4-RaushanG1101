#include "aiws/gui/main_window.hpp"

#include "aiws/document.hpp"
#include "aiws/gui/context_widget.hpp"
#include "aiws/gui/corpus_build_job.hpp"
#include "aiws/gui/remote_add_server.hpp"
#include "aiws/gui/resettable.hpp"
#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/term_inspector_widget.hpp"
#include "aiws/gui/workspace_widget.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <exception>
#include <utility>

namespace aiws::gui {

// ------------------------------------------------------------
// Corpus lifecycle State pattern
// ------------------------------------------------------------

class MainWindow::State {
public:
    virtual ~State() = default;

    virtual bool can_build() const {
        return false;
    }

    virtual bool corpus_current() const {
        return false;
    }

    virtual std::unique_ptr<State> build(MainWindow&) {
        return nullptr;
    }

    virtual std::unique_ptr<State> changed(MainWindow&);

    virtual std::unique_ptr<State> reset(MainWindow&);

    virtual std::unique_ptr<State> completed(
        MainWindow&,
        std::unique_ptr<ProcessingCore>,
        const QString&) {

        return nullptr;
    }
};

class MainWindow::EmptyState final : public State {
};

class MainWindow::DirtyState final : public State {
public:
    bool can_build() const override {
        return true;
    }

    std::unique_ptr<State> build(MainWindow&) override;
};

class MainWindow::BuildingState final : public State {
public:
    std::unique_ptr<State> changed(MainWindow&) override;

    std::unique_ptr<State> reset(MainWindow&) override;

    std::unique_ptr<State> completed(
        MainWindow& window,
        std::unique_ptr<ProcessingCore> core,
        const QString& error) override;
};

class MainWindow::ReadyState final : public State {
public:
    bool can_build() const override {
        return true;
    }

    bool corpus_current() const override {
        return true;
    }

    std::unique_ptr<State> build(MainWindow&) override;
};

std::unique_ptr<MainWindow::State>
MainWindow::State::changed(MainWindow&) {
    return std::make_unique<DirtyState>();
}

std::unique_ptr<MainWindow::State>
MainWindow::State::reset(MainWindow&) {
    return std::make_unique<EmptyState>();
}

std::unique_ptr<MainWindow::State>
MainWindow::DirtyState::build(MainWindow& window) {
    auto next = std::make_unique<BuildingState>();

    window.start_build();

    return next;
}

std::unique_ptr<MainWindow::State>
MainWindow::ReadyState::build(MainWindow& window) {
    auto next = std::make_unique<BuildingState>();

    window.start_build();

    return next;
}

std::unique_ptr<MainWindow::State>
MainWindow::BuildingState::changed(MainWindow& window) {
    window.cancel_build();

    return std::make_unique<DirtyState>();
}

std::unique_ptr<MainWindow::State>
MainWindow::BuildingState::reset(MainWindow& window) {
    window.cancel_build();

    return std::make_unique<EmptyState>();
}

std::unique_ptr<MainWindow::State>
MainWindow::BuildingState::completed(
    MainWindow& window,
    std::unique_ptr<ProcessingCore> core,
    const QString& error) {

    if (!error.isEmpty() || !core) {
        window.report_build_failure(
            error.isEmpty()
                ? QString("No result.")
                : error);

        return std::make_unique<DirtyState>();
    }

    window.accept_build(std::move(core));

    return std::make_unique<ReadyState>();
}

// ------------------------------------------------------------
// MainWindow construction and destruction
// ------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {

    setWindowTitle("AI Workspace");
    resize(900, 900);

    auto* central = new QWidget(this);
    setCentralWidget(central);

    workspace_widget_ = new WorkspaceWidget(central);
    search_widget_ = new SearchWidget(central);
    term_inspector_widget_ =
        new TermInspectorWidget(central);
    context_widget_ = new ContextWidget(central);

    status_label_ = new QLabel("Ready.", central);
    reset_button_ = new QPushButton("Reset", central);
    exit_button_ = new QPushButton("Exit", central);

    auto* header = new QLabel("AI Workspace", central);

    header->setAlignment(Qt::AlignCenter);

    header->setStyleSheet(
        "background-color: #24476a; color: white;"
        "font-size: 20px; font-weight: bold; padding: 10px;");

    reset_button_->setStyleSheet(
        "background-color: #d6e5f2; color: black; padding: 6px;");

    exit_button_->setStyleSheet(
        "background-color: #f2d6d6; color: black; padding: 6px;");

    auto* bottom_layout = new QHBoxLayout;

    bottom_layout->addWidget(term_inspector_widget_, 1);
    bottom_layout->addWidget(context_widget_, 1);

    auto* action_layout = new QHBoxLayout;

    action_layout->addWidget(status_label_, 1);
    action_layout->addWidget(reset_button_);
    action_layout->addWidget(exit_button_);

    auto* main_layout = new QVBoxLayout(central);

    main_layout->addWidget(header);
    main_layout->addWidget(workspace_widget_, 2);
    main_layout->addWidget(search_widget_, 3);
    main_layout->addLayout(bottom_layout, 2);
    main_layout->addLayout(action_layout);

    connect(workspace_widget_,
            &WorkspaceWidget::add_document_requested,
            this,
            &MainWindow::add_document);

    connect(workspace_widget_,
            &WorkspaceWidget::build_corpus_requested,
            this,
            &MainWindow::build_corpus);

    connect(workspace_widget_,
            &WorkspaceWidget::document_selected,
            this,
            &MainWindow::document_selected);

    connect(search_widget_,
            &SearchWidget::search_requested,
            this,
            &MainWindow::search_requested);

    connect(search_widget_,
            &SearchWidget::query_changed,
            this,
            &MainWindow::query_changed);

    connect(search_widget_,
            &SearchWidget::source_document_requested,
            this,
            &MainWindow::source_document_requested);

    connect(search_widget_,
            &SearchWidget::selected_chunk_changed,
            term_inspector_widget_,
            &TermInspectorWidget::set_selected_chunk);

    connect(context_widget_,
            &ContextWidget::build_context_requested,
            this,
            &MainWindow::build_context_requested);

    connect(term_inspector_widget_,
            &TermInspectorWidget::term_inspection_requested,
            this,
            &MainWindow::term_inspection_requested);

    connect(reset_button_,
            &QPushButton::clicked,
            this,
            &MainWindow::reset_application);

    connect(exit_button_,
            &QPushButton::clicked,
            this,
            &MainWindow::exit_application);

    // Initial corpus lifecycle state.
    state_ = std::make_unique<EmptyState>();

    // Create one worker for the application's lifetime.
    build_job_ = std::make_unique<CorpusBuildJob>();

    // Deliver worker completion to the GUI thread.
    connect(build_job_.get(),
            &CorpusBuildJob::completed,
            this,
            &MainWindow::check_build_completion,
            Qt::QueuedConnection);

    update_action_states();
}

MainWindow::~MainWindow() {
    if (remote_add_server_) {
        remote_add_server_->stop();
    }

    // The worker destructor requests shutdown and joins.
    build_job_.reset();
}

// ------------------------------------------------------------
// Public application API
// ------------------------------------------------------------

void MainWindow::set_exit_callback(
    ExitCallback callback) noexcept {

    exit_callback_ = callback;
}

void MainWindow::set_build_delay_ms(
    int ms_per_document) {

    build_delay_ms_ = std::max(0, ms_per_document);
}

bool MainWindow::start_ipc_server(
    const QString& socket_path) {

    if (remote_add_server_) {
        return false;
    }

    auto server =
        std::make_unique<RemoteAddServer>(socket_path);

    connect(server.get(),
            &RemoteAddServer::document_received,
            this,
            &MainWindow::add_document_from_ipc,
            Qt::QueuedConnection);

    if (!server->start()) {
        return false;
    }

    remote_add_server_ = std::move(server);

    return true;
}

// ------------------------------------------------------------
// Document addition and IPC
// ------------------------------------------------------------

void MainWindow::add_document_from_ipc(
    const QString& path) {

    add_document_file(path);
}

void MainWindow::add_document() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Add Document",
        QString(),
        "Text Files (*.txt);;All Files (*)");

    if (path.isEmpty()) {
        return;
    }

    add_document_file(path);
}

bool MainWindow::add_document_file(
    const QString& path) {

    QFile file(path);

    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray bytes = file.readAll();
    const QString text = QString::fromUtf8(bytes);
    const QFileInfo info(path);

    workspace_.add_document(Document(
        info.absoluteFilePath().toStdString(),
        info.fileName().toStdString(),
        text.toStdString()));

    workspace_widget_->add_document(info.fileName());

    ++workspace_revision_;

    invalidate_corpus();

    workspace_widget_->select_document(
        static_cast<int>(
            workspace_.documents().size()) - 1);

    status_label_->setText("Document added.");

    update_action_states();

    return true;
}

// ------------------------------------------------------------
// Background corpus building
// ------------------------------------------------------------

void MainWindow::build_corpus() {
    try {
        auto next = state_->build(*this);

        if (next) {
            state_ = std::move(next);

            clear_processing_outputs();
        }

    } catch (const std::exception& ex) {
        state_ = std::make_unique<DirtyState>();

        report_build_failure(
            QString::fromUtf8(ex.what()));
    }

    update_action_states();
}

void MainWindow::start_build() {
    build_job_->submit(
        workspace_,
        workspace_revision_,
        build_delay_ms_);

    workspace_widget_->set_corpus_status(
        "Corpus: Building...");

    status_label_->setText("Building corpus...");
}

void MainWindow::cancel_build() {
    build_job_->request_cancel();
}

void MainWindow::accept_build(
    std::unique_ptr<ProcessingCore> core) {

    processing_core_ = std::move(*core);

    workspace_widget_->set_corpus_status(
        "Corpus: " +
        QString::number(
            static_cast<qulonglong>(
                processing_core_.chunk_count())) +
        " chunks");

    status_label_->setText("Corpus built.");
}

void MainWindow::report_build_failure(
    const QString& error) {

    workspace_widget_->set_corpus_status(
        "Corpus: Build failed.");

    status_label_->setText(
        "Build failed: " + error);
}

void MainWindow::check_build_completion() {
    auto result = build_job_->take_result();

    if (!result) {
        return;
    }

#ifdef AIWS_FAULT_LOSE_FAILURE
    if (!result->error.empty()) {
        return;
    }
#endif

    bool current_revision =
        result->revision == workspace_revision_;

#ifdef AIWS_FAULT_ACCEPT_STALE
    current_revision = true;
#endif

    // Never install a result for an obsolete Workspace.
    if (!current_revision) {
        return;
    }

    auto next = state_->completed(
        *this,
        std::move(result->core),
        QString::fromStdString(result->error));

    if (next) {
        state_ = std::move(next);
    }

    update_action_states();
}

// ------------------------------------------------------------
// Document selection
// ------------------------------------------------------------

void MainWindow::document_selected(int row) {
    const auto& documents = workspace_.documents();

    if (row < 0 ||
        static_cast<std::size_t>(row) >= documents.size()) {

        workspace_widget_->clear_selected_document();

        return;
    }

    const Document& document =
        documents[static_cast<std::size_t>(row)];

    workspace_widget_->show_selected_document(
        QString::fromStdString(document.title()),
        QString::fromStdString(document.text()));
}

// ------------------------------------------------------------
// Search and context operations
// ------------------------------------------------------------

void MainWindow::search_requested(
    const QString& query, int k) {

    if (!state_->corpus_current() ||
        query.trimmed().isEmpty()) {

        return;
    }

    search_widget_->set_results(
        processing_core_.search(
            query.toStdString(), k));

    context_widget_->clear_context();

    term_inspector_widget_->set_selected_chunk(
        QString());

    term_inspector_widget_->clear_statistics();

    status_label_->setText("Search completed.");
}

void MainWindow::query_changed(
    const QString& query) {

    (void)query;

    update_action_states();
}

void MainWindow::build_context_requested(
    int token_budget) {

    if (!state_->corpus_current() ||
        !search_widget_->has_query()) {

        return;
    }

    const auto items = processing_core_.build_context(
        search_widget_->query().toStdString(),
        search_widget_->result_count(),
        static_cast<std::size_t>(token_budget));

    context_widget_->set_context(items);

    status_label_->setText("Context built.");
}

void MainWindow::source_document_requested(
    const QString& document_id) {

    const auto& documents = workspace_.documents();

    for (std::size_t i = 0;
         i < documents.size();
         ++i) {

        if (QString::fromStdString(
                documents[i].id()) == document_id) {

            workspace_widget_->select_document(
                static_cast<int>(i));

            return;
        }
    }
}

// ------------------------------------------------------------
// Term inspection
// ------------------------------------------------------------

void MainWindow::term_inspection_requested(
    const QString& term,
    const QString& chunk_id) {

    if (!state_->corpus_current() ||
        term.trimmed().isEmpty()) {

        return;
    }

    const std::size_t df =
        processing_core_.document_frequency(
            term.toStdString());

    const bool has_chunk = !chunk_id.isEmpty();

    std::size_t tf = 0;

    if (has_chunk) {
        tf = processing_core_.term_frequency(
            term.toStdString(),
            chunk_id.toStdString());
    }

    term_inspector_widget_->set_statistics(
        df, has_chunk, tf);

    status_label_->setText("Term inspected.");
}

// ------------------------------------------------------------
// Reset and Exit
// ------------------------------------------------------------

void MainWindow::reset_application() {
    workspace_.clear();

    ++workspace_revision_;

    auto next = state_->reset(*this);

    if (next) {
        state_ = std::move(next);
    }

    Resettable* components[] = {
        workspace_widget_,
        search_widget_,
        term_inspector_widget_,
        context_widget_
    };

    for (Resettable* component : components) {
        component->reset();
    }

    status_label_->setText("Application reset.");

    update_action_states();
}

void MainWindow::exit_application() {
    if (exit_callback_ != nullptr) {
        exit_callback_(this);
    }
}

// ------------------------------------------------------------
// GUI state and output management
// ------------------------------------------------------------

void MainWindow::update_action_states() {
    workspace_widget_->set_build_corpus_enabled(
        state_->can_build());

    search_widget_->set_search_enabled(
        state_->corpus_current() &&
        search_widget_->has_query());

    term_inspector_widget_->set_inspection_enabled(
        state_->corpus_current());

    context_widget_->set_build_context_enabled(
        state_->corpus_current() &&
        search_widget_->has_query());
}

void MainWindow::invalidate_corpus() {
    auto next = state_->changed(*this);

    if (next) {
        state_ = std::move(next);
    }

    workspace_widget_->set_corpus_status(
        "Corpus not built.");

    clear_processing_outputs();

    update_action_states();
}

void MainWindow::clear_processing_outputs() {
    search_widget_->clear_results();

    term_inspector_widget_->set_selected_chunk(
        QString());

    term_inspector_widget_->clear_statistics();

    context_widget_->clear_context();
}

}  // namespace aiws::gui