#include "aiws/gui/main_window.hpp"

#include "aiws/document.hpp"
#include "aiws/gui/context_widget.hpp"
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

#include <utility>

namespace aiws::gui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("AI Workspace");
    resize(900, 900);

    auto* central = new QWidget(this);
    setCentralWidget(central);

    workspace_widget_ = new WorkspaceWidget(central);
    search_widget_ = new SearchWidget(central);
    term_inspector_widget_ = new TermInspectorWidget(central);
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
            this, &MainWindow::add_document);

    connect(workspace_widget_,
            &WorkspaceWidget::build_corpus_requested,
            this, &MainWindow::build_corpus);

    connect(workspace_widget_,
            &WorkspaceWidget::document_selected,
            this, &MainWindow::document_selected);

    connect(search_widget_, &SearchWidget::search_requested,
            this, &MainWindow::search_requested);

    connect(search_widget_, &SearchWidget::query_changed,
            this, &MainWindow::query_changed);

    connect(search_widget_, &SearchWidget::source_document_requested,
            this, &MainWindow::source_document_requested);

    connect(search_widget_, &SearchWidget::selected_chunk_changed,
            term_inspector_widget_,
            &TermInspectorWidget::set_selected_chunk);

    connect(context_widget_, &ContextWidget::build_context_requested,
            this, &MainWindow::build_context_requested);

    connect(term_inspector_widget_,
            &TermInspectorWidget::term_inspection_requested,
            this, &MainWindow::term_inspection_requested);

    connect(reset_button_, &QPushButton::clicked,
            this, &MainWindow::reset_application);

    connect(exit_button_, &QPushButton::clicked,
            this, &MainWindow::exit_application);

    update_action_states();
}

void MainWindow::set_exit_callback(ExitCallback callback) noexcept {
    exit_callback_ = callback;
}

void MainWindow::add_document() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Add Document", QString(), "Text Files (*.txt);;All Files (*)");

    if (path.isEmpty()) {
        return;
    }

    QFile file(path);

    if (!file.open(QIODevice::ReadOnly)) {
        status_label_->setText("Could not open document.");
        return;
    }

    const QByteArray bytes = file.readAll();
    const QString text = QString::fromUtf8(bytes);
    const QFileInfo info(path);

    workspace_.add_document(Document(
        info.absoluteFilePath().toStdString(),
        info.fileName().toStdString(),
        text.toStdString()));

    workspace_widget_->add_document(info.fileName());

    invalidate_corpus();
    workspace_widget_->select_document(
        static_cast<int>(workspace_.documents().size()) - 1);

    status_label_->setText("Document added.");
    update_action_states();
}

void MainWindow::build_corpus() {
    if (workspace_.documents().empty()) {
        return;
    }

    processing_core_.rebuild(workspace_);
    corpus_current_ = true;

    clear_processing_outputs();

    workspace_widget_->set_corpus_status(
        "Corpus: " +
        QString::number(
            static_cast<qulonglong>(processing_core_.chunk_count())) +
        " chunks");

    status_label_->setText("Corpus built.");
    update_action_states();
}

void MainWindow::document_selected(int row) {
    const auto& documents = workspace_.documents();

    if (row < 0 || static_cast<std::size_t>(row) >= documents.size()) {
        workspace_widget_->clear_selected_document();
        return;
    }

    const Document& document = documents[static_cast<std::size_t>(row)];

    workspace_widget_->show_selected_document(
        QString::fromStdString(document.title()),
        QString::fromStdString(document.text()));
}

void MainWindow::search_requested(const QString& query, int k) {
    if (!corpus_current_ || query.trimmed().isEmpty()) {
        return;
    }

    search_widget_->set_results(
        processing_core_.search(query.toStdString(), k));

    context_widget_->clear_context();
    term_inspector_widget_->set_selected_chunk(QString());
    term_inspector_widget_->clear_statistics();

    status_label_->setText("Search completed.");
}

void MainWindow::query_changed(const QString& query) {
    (void)query;
    update_action_states();
}

void MainWindow::build_context_requested(int token_budget) {
    if (!corpus_current_ || !search_widget_->has_query()) {
        return;
    }

    const auto items = processing_core_.build_context(
        search_widget_->query().toStdString(),
        search_widget_->result_count(),
        static_cast<std::size_t>(token_budget));

    context_widget_->set_context(items);
    status_label_->setText("Context built.");
}

void MainWindow::source_document_requested(const QString& document_id) {
    const auto& documents = workspace_.documents();

    for (std::size_t i = 0; i < documents.size(); ++i) {
        if (QString::fromStdString(documents[i].id()) == document_id) {
            workspace_widget_->select_document(static_cast<int>(i));
            return;
        }
    }
}

void MainWindow::term_inspection_requested(
    const QString& term, const QString& chunk_id) {

    if (!corpus_current_ || term.trimmed().isEmpty()) {
        return;
    }

    const std::size_t df =
        processing_core_.document_frequency(term.toStdString());

    const bool has_chunk = !chunk_id.isEmpty();
    std::size_t tf = 0;

    if (has_chunk) {
        tf = processing_core_.term_frequency(
            term.toStdString(), chunk_id.toStdString());
    }

    term_inspector_widget_->set_statistics(df, has_chunk, tf);
    status_label_->setText("Term inspected.");
}

void MainWindow::reset_application() {
    workspace_.clear();
    processing_core_.rebuild(workspace_);
    corpus_current_ = false;

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

void MainWindow::update_action_states() {
    workspace_widget_->set_build_corpus_enabled(
        !workspace_.documents().empty());

    search_widget_->set_search_enabled(
        corpus_current_ && search_widget_->has_query());

    term_inspector_widget_->set_inspection_enabled(corpus_current_);

    context_widget_->set_build_context_enabled(
        corpus_current_ && search_widget_->has_query());
}

void MainWindow::invalidate_corpus() {
    corpus_current_ = false;

    workspace_widget_->set_corpus_status("Corpus not built.");
    clear_processing_outputs();
    update_action_states();
}

void MainWindow::clear_processing_outputs() {
    search_widget_->clear_results();
    term_inspector_widget_->set_selected_chunk(QString());
    term_inspector_widget_->clear_statistics();
    context_widget_->clear_context();
}

}  // namespace aiws::gui
