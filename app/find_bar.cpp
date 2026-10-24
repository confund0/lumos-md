#include "find_bar.hpp"

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTextCharFormat>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "lumos/qt/source_editor.hpp"

FindBar::FindBar(lumos::qt::SourceEditor* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(4, 2, 4, 2);

  auto* row1 = new QHBoxLayout;
  pattern_ = new QLineEdit(this);
  pattern_->setPlaceholderText(tr("Find"));
  case_sensitive_ = new QCheckBox(tr("Aa"), this);
  case_sensitive_->setToolTip(tr("Case-sensitive"));
  whole_word_ = new QCheckBox(tr("Word"), this);
  whole_word_->setToolTip(tr("Whole word"));
  regex_ = new QCheckBox(tr("Regex"), this);
  previous_ = new QToolButton(this);
  previous_->setText(tr("↑"));
  previous_->setToolTip(tr("Previous match"));
  next_ = new QToolButton(this);
  next_->setText(tr("↓"));
  next_->setToolTip(tr("Next match"));
  count_ = new QLabel(this);
  close_ = new QToolButton(this);
  close_->setText(tr("✕"));
  close_->setToolTip(tr("Close"));
  row1->addWidget(pattern_);
  row1->addWidget(case_sensitive_);
  row1->addWidget(whole_word_);
  row1->addWidget(regex_);
  row1->addWidget(previous_);
  row1->addWidget(next_);
  row1->addWidget(count_);
  row1->addWidget(close_);
  outer->addLayout(row1);

  auto* row2 = new QHBoxLayout;
  replacement_ = new QLineEdit(this);
  replacement_->setPlaceholderText(tr("Replace"));
  replace_ = new QPushButton(tr("Replace"), this);
  replace_all_ = new QPushButton(tr("Replace All"), this);
  row2->addWidget(replacement_);
  row2->addWidget(replace_);
  row2->addWidget(replace_all_);
  outer->addLayout(row2);

  hide();

  pattern_->installEventFilter(this);

  connect(pattern_, &QLineEdit::textChanged, this, [this](const QString&) { run_search(); });
  connect(case_sensitive_, &QCheckBox::toggled, this, [this](bool) { run_search(); });
  connect(whole_word_, &QCheckBox::toggled, this, [this](bool) { run_search(); });
  connect(regex_, &QCheckBox::toggled, this, [this](bool) { run_search(); });
  connect(previous_, &QToolButton::clicked, this, [this] { move_to(false); });
  connect(next_, &QToolButton::clicked, this, [this] { move_to(true); });
  connect(replace_, &QPushButton::clicked, this, [this] { do_replace(); });
  connect(replace_all_, &QPushButton::clicked, this, [this] { do_replace_all(); });
  connect(close_, &QToolButton::clicked, this, [this] { close_bar(); });
  connect(editor_, &lumos::qt::SourceEditor::textChanged, this, [this] { on_document_changed(); });

  auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  esc->setContext(Qt::WidgetWithChildrenShortcut);
  connect(esc, &QShortcut::activated, this, [this] { close_bar(); });
}

void FindBar::open_find() {
  replacement_->hide();
  replace_->hide();
  replace_all_->hide();
  show();
  pattern_->setFocus();
  pattern_->selectAll();
  run_search();
}

void FindBar::open_replace() {
  replacement_->show();
  replace_->show();
  replace_all_->show();
  show();
  pattern_->setFocus();
  pattern_->selectAll();
  run_search();
}

bool FindBar::eventFilter(QObject* watched, QEvent* event) {
  if (watched == pattern_ && event->type() == QEvent::KeyPress) {
    auto* key_event = static_cast<QKeyEvent*>(event);
    if (key_event->key() == Qt::Key_Return || key_event->key() == Qt::Key_Enter) {
      move_to(!key_event->modifiers().testFlag(Qt::ShiftModifier));
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

lumos::FindOptions FindBar::options() const {
  return {case_sensitive_->isChecked(), whole_word_->isChecked(), regex_->isChecked()};
}

void FindBar::recompute_matches() {
  const auto result = lumos::find_all(editor_->document(), pattern_->text().toStdString(), options());
  matches_ = result.value_or(std::vector<lumos::ByteRange>{});
}

std::optional<std::size_t> FindBar::nearest(std::size_t pos, bool forward, bool wrap) const {
  if (matches_.empty()) return std::nullopt;
  const auto it = std::lower_bound(matches_.begin(), matches_.end(), pos,
                                   [](const lumos::ByteRange& r, std::size_t v) { return r.start < v; });
  if (forward) {
    if (it != matches_.end()) return static_cast<std::size_t>(it - matches_.begin());
    return wrap ? std::optional<std::size_t>(0) : std::nullopt;
  }
  if (it != matches_.begin()) return static_cast<std::size_t>((it - matches_.begin()) - 1);
  return wrap ? std::optional<std::size_t>(matches_.size() - 1) : std::nullopt;
}

void FindBar::run_search() {
  recompute_matches();
  current_ = nearest(editor_->selection().cursor,  true,  true);
  update_marks();
  update_count_label();
}

void FindBar::move_to(bool forward) {
  recompute_matches();
  const lumos::Selection sel = editor_->selection();
  const std::size_t anchor = forward ? sel.cursor : std::min(sel.anchor, sel.cursor);
  current_ = nearest(anchor, forward,  true);
  if (current_) editor_->select(matches_[*current_].start, matches_[*current_].end);
  update_marks();
  update_count_label();
}

void FindBar::do_replace() {
  if (current_ && *current_ < matches_.size()) {
    const lumos::ByteRange match = matches_[*current_];
    const std::string pattern = pattern_->text().toStdString();
    const std::string replacement = replacement_->text().toStdString();
    if (editor_->apply_command(lumos::replace_one(editor_->document(), match, pattern, replacement, options()))) {
      const std::size_t pos = editor_->selection().cursor;
      recompute_matches();
      current_ = nearest(pos,  true,  false);
    } else {
      recompute_matches();
      current_ = nearest(editor_->selection().cursor,  true,  true);
    }
    if (current_) editor_->select(matches_[*current_].start, matches_[*current_].end);
  }
  update_marks();
  update_count_label();
}

void FindBar::do_replace_all() {
  const std::string pattern = pattern_->text().toStdString();
  const std::string replacement = replacement_->text().toStdString();
  editor_->apply_command(lumos::replace_all(editor_->document(), pattern, replacement, options()));
  recompute_matches();
  current_ = nearest(editor_->selection().cursor,  true,  true);
  update_marks();
  update_count_label();
}

void FindBar::on_document_changed() {
  matches_.clear();
  current_.reset();
  update_marks();
  update_count_label();
}

void FindBar::close_bar() {
  hide();
  matches_.clear();
  current_.reset();
  editor_->set_marks(2, {}, QTextCharFormat());
  editor_->set_marks(3, {}, QTextCharFormat());
  editor_->setFocus();
}

void FindBar::update_marks() {
  editor_->set_marks(2, matches_, editor_->style_table().find_hit);
  if (current_ && *current_ < matches_.size())
    editor_->set_marks(3, {matches_[*current_]}, editor_->style_table().find_hit_current);
  else
    editor_->set_marks(3, {}, QTextCharFormat());
}

void FindBar::update_count_label() {
  if (matches_.empty()) {
    count_->setText(tr("No matches"));
  } else if (current_ && *current_ < matches_.size()) {
    count_->setText(tr("%1/%2").arg(static_cast<int>(*current_ + 1)).arg(static_cast<int>(matches_.size())));
  } else {
    count_->setText(tr("-/%1").arg(static_cast<int>(matches_.size())));
  }
}
