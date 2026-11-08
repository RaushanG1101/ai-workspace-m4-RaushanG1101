#include "aiws/gui/context_widget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QString>
#include <QVBoxLayout>

namespace aiws::gui {

ContextWidget::ContextWidget(QWidget* parent) : QWidget(parent) {
    context_budget_slider_ = new QSlider(Qt::Horizontal, this);
    budget_value_label_ = new QLabel(this);
    build_context_button_ = new QPushButton("Build Context", this);
    context_text_ = new QPlainTextEdit(this);

    context_budget_slider_->setRange(50, 1000);
    context_budget_slider_->setSingleStep(50);
    context_budget_slider_->setPageStep(50);
    context_budget_slider_->setTickInterval(50);
    context_budget_slider_->setTickPosition(QSlider::TicksBelow);

    context_text_->setReadOnly(true);

    auto* budget_layout = new QHBoxLayout;
    budget_layout->addWidget(new QLabel("Token Budget:", this));
    budget_layout->addWidget(context_budget_slider_);
    budget_layout->addWidget(budget_value_label_);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->addWidget(new QLabel("Context", this));
    main_layout->addLayout(budget_layout);
    main_layout->addWidget(build_context_button_);
    main_layout->addWidget(context_text_);

    connect(context_budget_slider_, &QSlider::valueChanged,
            this, &ContextWidget::budget_changed);

    connect(build_context_button_, &QPushButton::clicked,
            this, &ContextWidget::request_context);

    reset();
}

int ContextWidget::token_budget() const {
    return context_budget_slider_->value();
}

void ContextWidget::set_build_context_enabled(bool enabled) {
    build_context_button_->setEnabled(enabled);
}

void ContextWidget::set_context(const std::vector<ContextItem>& items) {
    QString output;

    for (const ContextItem& item : items) {
        if (!output.isEmpty()) {
            output += "\n\n";
        }

        output += "Document ID: " +
                  QString::fromStdString(item.document_id) + "\n";

        output += "Chunk Sequence: " +
                  QString::number(
                      static_cast<qulonglong>(item.chunk_sequence)) + "\n";

        output += "Tokens: " +
                  QString::number(
                      static_cast<qulonglong>(item.token_count)) + "\n";

        output += "Score: " +
                  QString::number(item.score) + "\n";

        output += "Truncated: ";
        output += item.truncated ? "Yes\n" : "No\n";

        output += "\n" + QString::fromStdString(item.text);
    }

    context_text_->setPlainText(output);
}

void ContextWidget::clear_context() {
    context_text_->clear();
}

void ContextWidget::reset() {
    context_budget_slider_->setValue(300);
    budget_changed(300);

    clear_context();
    set_build_context_enabled(false);
}

void ContextWidget::request_context() {
    if (!build_context_button_->isEnabled()) {
        return;
    }

    emit build_context_requested(token_budget());
}

void ContextWidget::budget_changed(int value) {
    const int adjusted = 50 + ((value - 50 + 25) / 50) * 50;

    if (value != adjusted) {
        context_budget_slider_->setValue(adjusted);
        return;
    }

    budget_value_label_->setText(QString::number(adjusted));
}

}  // namespace aiws::gui
