#include "aiws/gui/term_inspector_widget.hpp"

#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace aiws::gui {

TermInspectorWidget::TermInspectorWidget(QWidget* parent)
    : QWidget(parent) {

    term_edit_ = new QLineEdit(this);
    inspect_button_ = new QPushButton("Inspect", this);

    document_frequency_value_ = new QLabel(this);
    chunk_frequency_value_ = new QLabel(this);

    term_edit_->setPlaceholderText("Enter a term");

    auto* input_layout = new QHBoxLayout;
    input_layout->addWidget(new QLabel("Term:", this));
    input_layout->addWidget(term_edit_);
    input_layout->addWidget(inspect_button_);

    auto* statistics_layout = new QGridLayout;
    statistics_layout->addWidget(
        new QLabel("Document Frequency:", this), 0, 0);
    statistics_layout->addWidget(document_frequency_value_, 0, 1);

    statistics_layout->addWidget(
        new QLabel("Selected Chunk TF:", this), 1, 0);
    statistics_layout->addWidget(chunk_frequency_value_, 1, 1);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->addWidget(new QLabel("Term Inspector", this));
    main_layout->addLayout(input_layout);
    main_layout->addLayout(statistics_layout);
    main_layout->addStretch();

    connect(inspect_button_, &QPushButton::clicked,
            this, &TermInspectorWidget::request_inspection);

    reset();
}

void TermInspectorWidget::set_inspection_enabled(bool enabled) {
    inspect_button_->setEnabled(enabled);
}

void TermInspectorWidget::set_selected_chunk(const QString& chunk_id) {
    selected_chunk_id_ = chunk_id;
}

void TermInspectorWidget::set_statistics(
    std::size_t document_frequency,
    bool has_chunk_frequency,
    std::size_t chunk_frequency) {

    document_frequency_value_->setText(
        QString::number(static_cast<qulonglong>(document_frequency)));

    if (has_chunk_frequency) {
        chunk_frequency_value_->setText(
            QString::number(static_cast<qulonglong>(chunk_frequency)));
    } else {
        chunk_frequency_value_->setText("N/A");
    }
}

void TermInspectorWidget::clear_statistics() {
    document_frequency_value_->setText("N/A");
    chunk_frequency_value_->setText("N/A");
}

void TermInspectorWidget::reset() {
    term_edit_->clear();
    selected_chunk_id_.clear();

    clear_statistics();
    set_inspection_enabled(false);
}

void TermInspectorWidget::request_inspection() {
    if (!inspect_button_->isEnabled()) {
        return;
    }

    const QString term = term_edit_->text().trimmed();

    if (term.isEmpty()) {
        return;
    }

    emit term_inspection_requested(term, selected_chunk_id_);
}

}  // namespace aiws::gui
