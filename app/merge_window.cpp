
#include "merge_window.hpp"

#include <QBrush>
#include <QColor>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace {

constexpr int kGutterPad = 6;

constexpr double kTint = 0.25;
constexpr double kTintCurrent = 0.45;

const QColor kGreen(0x2e, 0x7d, 0x32);
const QColor kBlue(0x15, 0x65, 0xc0);
const QColor kRed(0xc6, 0x28, 0x28);
const QColor kYellow(0xf9, 0xa8, 0x25);
const QColor kGrey(0x9e, 0x9e, 0x9e);

QColor mix(const QColor& base, const QColor& over, double amount) {
  const auto at = [&](int b, int o) { return static_cast<int>((b * (1 - amount)) + (o * amount)); };
  return QColor(at(base.red(), over.red()), at(base.green(), over.green()), at(base.blue(), over.blue()));
}

enum class RowTint { none, green, blue, red, grey, yellow };

struct Row {
  QString text;
  QString number;
  RowTint tint = RowTint::none;
  bool current = false;
};

QString shown_text(std::string_view line) {
  if (!line.empty() && line.back() == '\n') line.remove_suffix(1);
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  return QString::fromUtf8(line.data(), static_cast<int>(line.size()));
}

}  

class MergeWindow::LineView final : public QPlainTextEdit {
 public:
  explicit LineView(QWidget* parent) : QPlainTextEdit(parent), gutter_(new QWidget(this)) {
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    gutter_->installEventFilter(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { update_gutter_width(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
      if (dy != 0) {
        gutter_->scroll(0, dy);
      } else {
        gutter_->update(0, rect.y(), gutter_->width(), rect.height());
      }
    });
  }

  void set_rows(const std::vector<Row>& rows) {
    QStringList lines;
    lines.reserve(static_cast<qsizetype>(rows.size()));
    numbers_.clear();
    numbers_.reserve(rows.size());
    for (const Row& row : rows) {
      lines.push_back(row.text);
      numbers_.push_back(row.number);
    }
    setPlainText(lines.join(QLatin1Char('\n')));
    update_gutter_width();
    gutter_->update();

    const QColor base_bg = palette().color(QPalette::Base);
    QList<QTextEdit::ExtraSelection> selections;
    QTextBlock block = document()->firstBlock();
    for (std::size_t i = 0; i < rows.size() && block.isValid(); ++i, block = block.next()) {
      const Row& row = rows[i];
      if (row.tint == RowTint::none) continue;
      const double amount = row.current ? kTintCurrent : kTint;
      if (row.tint == RowTint::grey) {
        const QColor fill = mix(base_bg, kGrey, amount);
        QTextCharFormat fill_format;
        fill_format.setBackground(fill);
        fill_format.setProperty(QTextFormat::FullWidthSelection, true);
        selections.push_back({QTextCursor(block), fill_format});

        QTextCharFormat hatch_format;
        hatch_format.setBackground(QBrush(fill.darker(130), Qt::BDiagPattern));
        hatch_format.setProperty(QTextFormat::FullWidthSelection, true);
        selections.push_back({QTextCursor(block), hatch_format});
        continue;
      }
      QColor over;
      switch (row.tint) {
        case RowTint::green:
          over = kGreen;
          break;
        case RowTint::blue:
          over = kBlue;
          break;
        case RowTint::red:
          over = kRed;
          break;
        case RowTint::yellow:
          over = kYellow;
          break;
        case RowTint::none:
        case RowTint::grey:
          continue;  
      }
      QTextCharFormat format;
      format.setBackground(mix(base_bg, over, amount));
      format.setProperty(QTextFormat::FullWidthSelection, true);
      QTextEdit::ExtraSelection selection;
      selection.cursor = QTextCursor(block);
      selection.format = format;
      selections.push_back(selection);
    }
    setExtraSelections(selections);
  }

 protected:
  void resizeEvent(QResizeEvent* event) override {
    QPlainTextEdit::resizeEvent(event);
    update_gutter_width();
  }

  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == gutter_ && event->type() == QEvent::Paint) {
      paint_gutter(static_cast<QPaintEvent*>(event));
      return true;
    }
    return QPlainTextEdit::eventFilter(watched, event);
  }

 private:
  int gutter_width() const {
    qsizetype widest = 1;
    for (const QString& number : numbers_) widest = std::max(widest, number.size());
    return (2 * kGutterPad) + (fontMetrics().horizontalAdvance(QLatin1Char('9')) * static_cast<int>(widest));
  }

  void update_gutter_width() {
    setViewportMargins(gutter_width(), 0, 0, 0);
    const QRect area = contentsRect();
    gutter_->setGeometry(QRect(area.left(), area.top(), gutter_width(), area.height()));
  }

  void paint_gutter(QPaintEvent* event) {
    QPainter painter(gutter_);
    painter.fillRect(event->rect(), palette().color(QPalette::Window));
    painter.setPen(palette().color(QPalette::WindowText));
    QTextBlock block = firstVisibleBlock();
    auto top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
    auto bottom = top + static_cast<int>(blockBoundingRect(block).height());
    while (block.isValid() && top <= event->rect().bottom()) {
      const auto at = static_cast<std::size_t>(block.blockNumber());
      if (block.isVisible() && bottom >= event->rect().top() && at < numbers_.size()) {
        painter.drawText(0, top, gutter_->width() - kGutterPad, fontMetrics().height(), Qt::AlignRight,
                          numbers_.at(at));
      }
      block = block.next();
      top = bottom;
      bottom = top + static_cast<int>(blockBoundingRect(block).height());
    }
  }

  QWidget* gutter_;
  std::vector<QString> numbers_;
};

namespace {

RowTint yours_pane_tint(ChunkKind kind, Pick pick) {
  if (kind == ChunkKind::same) return RowTint::none;
  if (kind == ChunkKind::both_same) return RowTint::green;
  if (kind == ChunkKind::conflict && pick == Pick::open) return RowTint::red;
  return (pick == Pick::yours || pick == Pick::both) ? RowTint::green : RowTint::yellow;
}
RowTint other_pane_tint(ChunkKind kind, Pick pick) {
  if (kind == ChunkKind::same) return RowTint::none;
  if (kind == ChunkKind::both_same) return RowTint::blue;
  if (kind == ChunkKind::conflict && pick == Pick::open) return RowTint::red;
  return (pick == Pick::other || pick == Pick::both) ? RowTint::blue : RowTint::yellow;
}

void push_pane_rows(std::vector<Row>* out, const std::vector<std::string_view>& lines, int begin, int end, int rows,
                     RowTint tint, bool current) {
  const int n = end - begin;
  for (int i = 0; i < rows; ++i) {
    Row row;
    row.current = current;
    if (i < n) {
      row.text = shown_text(lines[static_cast<std::size_t>(begin + i)]);
      row.number = QString::number(begin + i + 1);
      row.tint = tint;
    } else {
      row.tint = tint == RowTint::red ? RowTint::red : RowTint::grey;
    }
    out->push_back(std::move(row));
  }
}

void push_result_rows(std::vector<Row>* out, const LineMerge& merge, const MergeChunk& chunk, Pick pick, int rows,
                       bool current, int* number) {
  std::vector<std::pair<std::string_view, RowTint>> real;
  const auto take = [&](const std::vector<std::string_view>& lines, int begin, int end, RowTint tint) {
    for (int i = begin; i < end; ++i) real.emplace_back(lines[static_cast<std::size_t>(i)], tint);
  };
  if (chunk.kind == ChunkKind::same || chunk.kind == ChunkKind::both_same) {
    take(merge.yours, chunk.yours_begin, chunk.yours_end, RowTint::none);
  } else {
    if (pick == Pick::yours || pick == Pick::both) take(merge.yours, chunk.yours_begin, chunk.yours_end, RowTint::green);
    if (pick == Pick::other || pick == Pick::both) take(merge.other, chunk.other_begin, chunk.other_end, RowTint::blue);
  }
  for (int i = 0; i < rows; ++i) {
    Row row;
    row.current = current;
    if (static_cast<std::size_t>(i) < real.size()) {
      row.text = shown_text(real[static_cast<std::size_t>(i)].first);
      row.number = QString::number(++*number);
      row.tint = real[static_cast<std::size_t>(i)].second;
    } else {
      row.tint = pick == Pick::open ? RowTint::red : RowTint::grey;
    }
    out->push_back(std::move(row));
  }
}

}  

MergeWindow::MergeWindow(const LineMerge& merge, const QString& name, const MergeNames& names, QWidget* parent)
    : QDialog(parent), merge_(merge), names_(names) {
  setWindowTitle(tr("Merge %1").arg(name));
  QSize size(1200, 800);
  if (parent != nullptr) size = size.boundedTo(parent->size());
  resize(size);

  chunk_conflict_ordinal_.reserve(merge_.chunks.size());
  for (std::size_t ci = 0; ci < merge_.chunks.size(); ++ci) {
    if (merge_.chunks[ci].kind == ChunkKind::conflict) {
      chunk_conflict_ordinal_.push_back(static_cast<int>(ordinal_to_chunk_.size()));
      ordinal_to_chunk_.push_back(static_cast<int>(ci));
    } else {
      chunk_conflict_ordinal_.push_back(-1);
    }
  }
  picks_ = default_picks(merge_);
  current_conflict_ = ordinal_to_chunk_.empty() ? -1 : 0;

  auto* layout = new QVBoxLayout(this);
  auto* splitter = new QSplitter(Qt::Vertical, this);

  const auto make_title = [](QWidget* parent_widget, const QString& text) {
    auto* label = new QLabel(text, parent_widget);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
  };

  auto* top = new QWidget(splitter);
  auto* grid = new QGridLayout(top);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setColumnStretch(0, 1);
  grid->setColumnStretch(1, 0);
  grid->setColumnStretch(2, 1);

  yours_view_ = new LineView(top);
  other_view_ = new LineView(top);
  grid->addWidget(make_title(top, tr("Your text")), 0, 0);
  grid->addWidget(make_title(top, names.other_title), 0, 2);
  grid->addWidget(yours_view_, 1, 0);
  grid->addWidget(other_view_, 1, 2);

  use_this_yours_button_ = new QPushButton(tr("Use this ↓"), top);
  use_this_yours_button_->setToolTip(tr("Use this text for this conflict"));
  use_all_yours_button_ = new QPushButton(tr("Use all ↓"), top);
  use_all_yours_button_->setToolTip(tr("Use all of this text"));
  use_this_other_button_ = new QPushButton(tr("Use this ↓"), top);
  use_this_other_button_->setToolTip(tr("Use this text for this conflict"));
  use_all_other_button_ = new QPushButton(tr("Use all ↓"), top);
  use_all_other_button_->setToolTip(tr("Use all of this text"));
  use_both_button_ = new QPushButton(tr("Use both ↓"), top);
  use_both_button_->setToolTip(tr("Your text first, then the other text"));

  auto* yours_buttons = new QHBoxLayout;
  yours_buttons->addStretch();
  yours_buttons->addWidget(use_this_yours_button_);
  yours_buttons->addWidget(use_all_yours_button_);
  yours_buttons->addStretch();
  grid->addLayout(yours_buttons, 2, 0);
  grid->addWidget(use_both_button_, 2, 1, Qt::AlignCenter);
  auto* other_buttons = new QHBoxLayout;
  other_buttons->addStretch();
  other_buttons->addWidget(use_this_other_button_);
  other_buttons->addWidget(use_all_other_button_);
  other_buttons->addStretch();
  grid->addLayout(other_buttons, 2, 2);

  auto* result_box = new QWidget(splitter);
  auto* result_layout = new QVBoxLayout(result_box);
  result_layout->setContentsMargins(0, 0, 0, 0);
  result_layout->addWidget(make_title(result_box, tr("Result")));
  result_view_ = new LineView(result_box);
  result_layout->addWidget(result_view_);

  splitter->addWidget(top);
  splitter->addWidget(result_box);
  layout->addWidget(splitter);

  auto* label_row = new QHBoxLayout;
  undo_button_ = new QPushButton(tr("↶ Undo"), this);
  redo_button_ = new QPushButton(tr("↷ Redo"), this);
  label_row->addWidget(undo_button_);
  label_row->addWidget(redo_button_);
  conflict_label_ = new QLabel(this);
  label_row->addWidget(conflict_label_);
  label_row->addStretch();
  layout->addLayout(label_row);

  auto* buttons = new QDialogButtonBox(this);
  use_button_ = buttons->addButton(QDialogButtonBox::Ok);
  buttons->addButton(QDialogButtonBox::Cancel);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  connect(use_this_yours_button_, &QPushButton::clicked, this, [this] { pick(Pick::yours); });
  connect(use_all_yours_button_, &QPushButton::clicked, this, [this] { use_all(Pick::yours); });
  connect(use_this_other_button_, &QPushButton::clicked, this, [this] { pick(Pick::other); });
  connect(use_all_other_button_, &QPushButton::clicked, this, [this] { use_all(Pick::other); });
  connect(use_both_button_, &QPushButton::clicked, this, [this] { pick(Pick::both); });

  connect(undo_button_, &QPushButton::clicked, this, [this] { undo(); });
  connect(redo_button_, &QPushButton::clicked, this, [this] { redo(); });

  auto* undo_shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z), this);
  undo_shortcut->setContext(Qt::WindowShortcut);
  connect(undo_shortcut, &QShortcut::activated, this, [this] { undo(); });
  auto* redo_shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y), this);
  redo_shortcut->setContext(Qt::WindowShortcut);
  connect(redo_shortcut, &QShortcut::activated, this, [this] { redo(); });
  auto* redo_shortcut_shift_z = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), this);
  redo_shortcut_shift_z->setContext(Qt::WindowShortcut);
  connect(redo_shortcut_shift_z, &QShortcut::activated, this, [this] { redo(); });

  yours_view_->viewport()->setMouseTracking(true);
  other_view_->viewport()->setMouseTracking(true);
  auto syncing = std::make_shared<bool>(false);
  for (LineView* view : {yours_view_, other_view_, result_view_}) {
    view->viewport()->installEventFilter(this);
    connect(view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this, view, syncing](int value) {
      if (*syncing) return;
      *syncing = true;
      for (LineView* other : {yours_view_, other_view_, result_view_}) {
        if (other != view) other->verticalScrollBar()->setValue(value);
      }
      *syncing = false;
    });
  }

  rebuild();
  scroll_to_current_conflict();
}

void MergeWindow::rebuild() {
  const int old_scroll = result_view_->verticalScrollBar()->value();

  std::vector<Row> yours_rows, other_rows, result_rows;
  row_chunk_.clear();
  chunk_first_row_.assign(merge_.chunks.size(), 0);

  int result_number = 0;
  for (std::size_t ci = 0; ci < merge_.chunks.size(); ++ci) {
    const MergeChunk& chunk = merge_.chunks[ci];
    chunk_first_row_[ci] = static_cast<int>(yours_rows.size());

    const int yours_n = chunk.yours_end - chunk.yours_begin;
    const int other_n = chunk.other_end - chunk.other_begin;
    const Pick pick = picks_[ci];
    const int conflict_ord = chunk_conflict_ordinal_[ci];
    const bool current = conflict_ord >= 0 && conflict_ord == current_conflict_;

    int result_n = 0;
    if (chunk.kind == ChunkKind::same || chunk.kind == ChunkKind::both_same) {
      result_n = yours_n;
    } else {
      switch (pick) {
        case Pick::yours:
          result_n = yours_n;
          break;
        case Pick::other:
          result_n = other_n;
          break;
        case Pick::both:
          result_n = yours_n + other_n;
          break;
        case Pick::open:
          result_n = std::max(yours_n, other_n);
          break;
      }
    }

    const int rows = std::max({yours_n, other_n, result_n});
    for (int i = 0; i < rows; ++i) row_chunk_.push_back(static_cast<int>(ci));

    push_pane_rows(&yours_rows, merge_.yours, chunk.yours_begin, chunk.yours_end, rows,
                   yours_pane_tint(chunk.kind, pick), current);
    push_pane_rows(&other_rows, merge_.other, chunk.other_begin, chunk.other_end, rows,
                   other_pane_tint(chunk.kind, pick), current);
    push_result_rows(&result_rows, merge_, chunk, pick, rows, current, &result_number);
  }

  yours_view_->set_rows(yours_rows);
  other_view_->set_rows(other_rows);
  result_view_->set_rows(result_rows);
  result_view_->verticalScrollBar()->setValue(old_scroll);

  const int total = conflicts();
  const int open = open_conflicts();
  QString label_text;
  if (total == 0) {
    label_text = tr("No conflicts: all changes went in by themselves.");
  } else if (open == 0) {
    label_text = tr("All chosen.");
  } else if (open == 1) {
    label_text = tr("1 conflict left to choose");
  } else {
    label_text = tr("%1 conflicts left to choose").arg(open);
  }
  conflict_label_->setText(label_text);
  use_button_->setEnabled(open == 0);
  const bool has_current = current_conflict_ >= 0;
  use_this_yours_button_->setEnabled(has_current);
  use_this_other_button_->setEnabled(has_current);
  use_both_button_->setEnabled(has_current);
  undo_button_->setEnabled(can_undo());
  redo_button_->setEnabled(can_redo());
}

void MergeWindow::scroll_to_current_conflict() {
  if (current_conflict_ < 0) {
    result_view_->verticalScrollBar()->setValue(0);
    return;
  }
  const int chunk_index = ordinal_to_chunk_.at(static_cast<std::size_t>(current_conflict_));
  result_view_->verticalScrollBar()->setValue(
      std::max(0, chunk_first_row_.at(static_cast<std::size_t>(chunk_index)) - 3));
}

void MergeWindow::move_to_next_open_after(int start) {
  const int n = conflicts();
  if (n == 0) {
    current_conflict_ = -1;
    return;
  }
  for (int step = 1; step <= n; ++step) {
    const int idx = (start + step) % n;
    const int chunk_index = ordinal_to_chunk_[static_cast<std::size_t>(idx)];
    if (picks_[static_cast<std::size_t>(chunk_index)] == Pick::open) {
      current_conflict_ = idx;
      return;
    }
  }
}

void MergeWindow::apply_pick(int ordinal, Pick chosen) {
  const int chunk_index = ordinal_to_chunk_.at(static_cast<std::size_t>(ordinal));
  const std::vector<Pick> before_picks = picks_;
  const int before_conflict = current_conflict_;
  picks_[static_cast<std::size_t>(chunk_index)] = chosen;
  current_conflict_ = ordinal;
  move_to_next_open_after(ordinal);
  commit_undo_step(before_picks, before_conflict);
  rebuild();
  scroll_to_current_conflict();
}

void MergeWindow::commit_undo_step(const std::vector<Pick>& before_picks, int before_conflict) {
  if (picks_ == before_picks) return;  
  undo_stack_.push_back({before_picks, before_conflict});
  redo_stack_.clear();
}

void MergeWindow::undo() {
  if (undo_stack_.empty()) return;
  redo_stack_.push_back({picks_, current_conflict_});
  picks_ = undo_stack_.back().picks;
  current_conflict_ = undo_stack_.back().current_conflict;
  undo_stack_.pop_back();
  rebuild();  
}

void MergeWindow::redo() {
  if (redo_stack_.empty()) return;
  undo_stack_.push_back({picks_, current_conflict_});
  picks_ = redo_stack_.back().picks;
  current_conflict_ = redo_stack_.back().current_conflict;
  redo_stack_.pop_back();
  rebuild();
}

bool MergeWindow::can_undo() const { return !undo_stack_.empty(); }

bool MergeWindow::can_redo() const { return !redo_stack_.empty(); }

std::string MergeWindow::result() const { return merge_result(merge_, picks_); }

int MergeWindow::conflicts() const { return static_cast<int>(ordinal_to_chunk_.size()); }

int MergeWindow::open_conflicts() const {
  int open = 0;
  for (int chunk_index : ordinal_to_chunk_) {
    if (picks_[static_cast<std::size_t>(chunk_index)] == Pick::open) ++open;
  }
  return open;
}

int MergeWindow::current_conflict() const { return current_conflict_; }

void MergeWindow::pick(Pick chosen) {
  if (current_conflict_ < 0) return;
  apply_pick(current_conflict_, chosen);
}

void MergeWindow::use_all(Pick side) {
  const std::vector<Pick> before_picks = picks_;
  const int before_conflict = current_conflict_;
  for (std::size_t ci = 0; ci < merge_.chunks.size(); ++ci) {
    const ChunkKind kind = merge_.chunks[ci].kind;
    if (kind == ChunkKind::same || kind == ChunkKind::both_same) continue;
    picks_[ci] = side;
  }
  current_conflict_ = -1;
  commit_undo_step(before_picks, before_conflict);
  rebuild();
}

QPlainTextEdit* MergeWindow::pane(Pane which) const {
  switch (which) {
    case Pane::yours:
      return yours_view_;
    case Pane::other:
      return other_view_;
    case Pane::result:
      return result_view_;
  }
  return nullptr;
}

QPushButton* MergeWindow::use_button() const { return use_button_; }

QPushButton* MergeWindow::use_this_button(Pane side) const {
  if (side == Pane::yours) return use_this_yours_button_;
  if (side == Pane::other) return use_this_other_button_;
  return nullptr;
}

QPushButton* MergeWindow::use_all_button(Pane side) const {
  if (side == Pane::yours) return use_all_yours_button_;
  if (side == Pane::other) return use_all_other_button_;
  return nullptr;
}

QPushButton* MergeWindow::use_both_button() const { return use_both_button_; }

QLabel* MergeWindow::conflict_label() const { return conflict_label_; }

QPushButton* MergeWindow::undo_button() const { return undo_button_; }

QPushButton* MergeWindow::redo_button() const { return redo_button_; }

bool MergeWindow::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::MouseButtonPress) {
    for (LineView* view : {yours_view_, other_view_, result_view_}) {
      if (watched != view->viewport()) continue;
      auto* mouse = static_cast<QMouseEvent*>(event);
      if (mouse->button() != Qt::LeftButton) break;
      const int block = view->cursorForPosition(mouse->pos()).blockNumber();
      if (block < 0 || static_cast<std::size_t>(block) >= row_chunk_.size()) break;
      const int chunk_index = row_chunk_[static_cast<std::size_t>(block)];
      const ChunkKind kind = merge_.chunks[static_cast<std::size_t>(chunk_index)].kind;
      const int ordinal = chunk_conflict_ordinal_[static_cast<std::size_t>(chunk_index)];
      const bool clickable = kind != ChunkKind::same && kind != ChunkKind::both_same;
      if (view != result_view_ && clickable) {
        const Pick side = view == yours_view_ ? Pick::yours : Pick::other;
        if (ordinal >= 0) {
          apply_pick(ordinal, side);
        } else {
          const std::vector<Pick> before_picks = picks_;
          const int before_conflict = current_conflict_;
          picks_[static_cast<std::size_t>(chunk_index)] = side;
          commit_undo_step(before_picks, before_conflict);
          rebuild();
        }
      } else if (ordinal >= 0) {
        current_conflict_ = ordinal;
        rebuild();
      }
      break;
    }
  } else if (event->type() == QEvent::MouseMove) {
    for (LineView* view : {yours_view_, other_view_}) {
      if (watched != view->viewport()) continue;
      const int block = view->cursorForPosition(static_cast<QMouseEvent*>(event)->pos()).blockNumber();
      bool clickable = false;
      if (block >= 0 && static_cast<std::size_t>(block) < row_chunk_.size()) {
        const ChunkKind kind = merge_.chunks[static_cast<std::size_t>(row_chunk_[static_cast<std::size_t>(block)])].kind;
        clickable = kind != ChunkKind::same && kind != ChunkKind::both_same;
      }
      view->viewport()->setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
      view->viewport()->setToolTip(clickable ? tr("Click to use this text") : QString());
      break;
    }
  }
  return QDialog::eventFilter(watched, event);
}
