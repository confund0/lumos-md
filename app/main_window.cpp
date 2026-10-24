
#include "main_window.hpp"

#include <QAction>
#include <QComboBox>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFontDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLineEdit>
#include <QLabel>
#include <QLocale>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QTextCharFormat>
#include <QShortcut>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>
#include <string>
#include <string_view>
#include <utility>

#include "find_bar.hpp"
#include "recent_files.hpp"
#include "toolbar_icons.hpp"
#include "lumos/commands.hpp"
#include "lumos/log.hpp"
#include "lumos/qt/source_editor.hpp"
#include "lumos/tools.hpp"

namespace {

constexpr std::string_view kComp = "app";

const QString kGeometry = QStringLiteral("window/geometry");
const QString kWindowState = QStringLiteral("window/state");
const QString kStatusBar = QStringLiteral("view/status_bar");
const QString kLineNumbers = QStringLiteral("view/line_numbers");
const QString kLimitWidth = QStringLiteral("view/limit_text_width");
const QString kPaperLook = QStringLiteral("view/paper_look");
const QString kFontFamily = QStringLiteral("view/font_family");
const QString kFontSize = QStringLiteral("view/font_size");
const QString kZoom = QStringLiteral("view/zoom");
const QString kRecentFiles = QStringLiteral("recent/files");

constexpr double kMinZoom = 0.5;
constexpr double kMaxZoom = 3.0;
constexpr double kMinFontSize = 4;
constexpr double kMaxFontSize = 200;

double round_zoom(double zoom) { return std::clamp(std::round(zoom * 10) / 10, kMinZoom, kMaxZoom); }

std::string_view status_name(QSettings::Status s) {
  switch (s) {
    case QSettings::NoError: return "ok";
    case QSettings::AccessError: return "access_error";
    case QSettings::FormatError: return "format_error";
  }
  return "unknown";
}

QColor mix(const QColor& a, const QColor& b, double t) {
  auto ch = [t](int x, int y) { return static_cast<int>(x + (y - x) * t + 0.5); };
  return QColor(ch(a.red(), b.red()), ch(a.green(), b.green()), ch(a.blue(), b.blue()));
}

lumos::qt::StyleTable paper_styles(const QFont& font, const QPalette& palette) {
  lumos::qt::StyleTable t = lumos::qt::StyleTable::make_default(font, palette);
  const QColor back = palette.color(QPalette::Active, QPalette::Base);
  const QColor text = palette.color(QPalette::Active, QPalette::Text);
  const QColor paper = mix(back, QColor(0xc8, 0xa0, 0x58), 0.2);
  QImage grain(128, 128, QImage::Format_RGB32);
  std::mt19937 rng(7);
  std::uniform_int_distribution<int> noise(-5, 5);
  for (int y = 0; y < grain.height(); ++y)
    for (int x = 0; x < grain.width(); ++x) {
      const int n = noise(rng);
      grain.setPixelColor(x, y, QColor(std::clamp(paper.red() + n, 0, 255), std::clamp(paper.green() + n, 0, 255),
                                       std::clamp(paper.blue() + n, 0, 255)));
    }
  t.page_background = QBrush(grain);
  t.outside_background = mix(back, text, 0.15);
  t.gutter_background = mix(paper, text, 0.08);
  t.gutter_text = mix(text, paper, 0.5);
  return t;
}

class TableGridPicker : public QWidget {
 public:
  explicit TableGridPicker(std::function<void(int, int)> pick, QWidget* parent = nullptr)
      : QWidget(parent), pick_(std::move(pick)) {
    setMouseTracking(true);
    setFixedSize(kMax * kCell + 2 * kMargin, kMax * kCell + 2 * kMargin + kLabelHeight);
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    for (int r = 0; r < kMax; ++r) {
      for (int c = 0; c < kMax; ++c) {
        const QRect cell(kMargin + c * kCell, kMargin + r * kCell, kCell - 2, kCell - 2);
        p.fillRect(cell, r < rows_ && c < cols_ ? palette().highlight() : palette().button());
        p.drawRect(cell);
      }
    }
    const QRect label(0, kMax * kCell + kMargin, width(), kLabelHeight);
    p.drawText(label, Qt::AlignCenter,
              rows_ > 0 ? QStringLiteral("%1 x %2").arg(rows_).arg(cols_) : tr("Table"));
  }
  void mouseMoveEvent(QMouseEvent* e) override { set_from_pos(e->pos()); }
  void mousePressEvent(QMouseEvent* e) override { set_from_pos(e->pos()); }
  void mouseReleaseEvent(QMouseEvent* e) override {
    set_from_pos(e->pos());
    if (rows_ > 0 && cols_ > 0 && pick_) pick_(rows_, cols_);
  }
  void leaveEvent(QEvent*) override {
    rows_ = cols_ = 0;
    update();
  }

 private:
  static constexpr int kMax = 8;
  static constexpr int kCell = 22;
  static constexpr int kMargin = 6;
  static constexpr int kLabelHeight = 20;

  void set_from_pos(const QPoint& pos) {
    rows_ = std::clamp((pos.y() - kMargin) / kCell + 1, 0, kMax);
    cols_ = std::clamp((pos.x() - kMargin) / kCell + 1, 0, kMax);
    update();
  }

  std::function<void(int, int)> pick_;
  int rows_ = 0;
  int cols_ = 0;
};

}  

MainWindow::MainWindow(const QString& settings_file) : settings_(settings_file, QSettings::IniFormat) {
  lumos::log(lumos::LogLevel::debug, kComp, "settings",
             {{"status", status_name(settings_.status())}, {"file", settings_file.toStdString()}});
  editor_ = new lumos::qt::SourceEditor(this);
  const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  const double default_size = fixed.pointSizeF() > 0 ? fixed.pointSizeF() + 2 : 12;
  font_family_ = settings_.value(kFontFamily, fixed.family()).toString();
  font_size_ = settings_.value(kFontSize, default_size).toDouble();
  if (!(font_size_ >= kMinFontSize && font_size_ <= kMaxFontSize)) font_size_ = default_size;
  zoom_ = settings_.value(kZoom, 1.0).toDouble();
  zoom_ = zoom_ >= kMinZoom && zoom_ <= kMaxZoom ? round_zoom(zoom_) : 1.0;
  apply_font();
  editor_->viewport()->installEventFilter(this);

  auto* central = new QWidget(this);
  auto* central_layout = new QVBoxLayout(central);
  central_layout->setContentsMargins(0, 0, 0, 0);
  central_layout->setSpacing(0);
  central_layout->addWidget(editor_);
  find_bar_ = new FindBar(editor_, central);
  central_layout->addWidget(find_bar_);
  setCentralWidget(central);
  if (!restoreGeometry(settings_.value(kGeometry).toByteArray())) resize(900, 700);

  QMenu* file = menuBar()->addMenu(tr("&File"));
  QAction* new_action = file->addAction(tr("&New"), this, [this] { new_file(); });
  new_action->setShortcut(QKeySequence::New);
  QAction* open = file->addAction(tr("&Open..."), this, [this] { open_dialog(); });
  open->setShortcut(QKeySequence::Open);
  recent_menu_ = file->addMenu(tr("Open &Recent"));
  connect(recent_menu_, &QMenu::aboutToShow, this, [this] { fill_recent_menu(); });
  fill_recent_menu();
  QAction* save_action = file->addAction(tr("&Save"), this, [this] { save(); });
  save_action->setShortcut(QKeySequence::Save);
  QAction* save_as_action = file->addAction(tr("Save &As..."), this, [this] { save_as(); });
  save_as_action->setShortcut(QKeySequence::SaveAs);
  revert_ = file->addAction(tr("Re&vert"), this, [this] { revert(); });
  file->addSeparator();
  file->addAction(tr("&Properties..."), this, [this] { show_properties(); });
  file->addSeparator();
  QAction* quit = file->addAction(tr("&Quit"), this, [this] { close(); });
  QToolBar* standard_bar = addToolBar(tr("Standard"));
  standard_bar->setObjectName(QStringLiteral("standard"));
  const auto add_standard = [&](QAction* a, ToolIcon icon) {
    a->setIcon(toolbar_icon(icon));
    standard_bar->addAction(a);
  };
  add_standard(new_action, ToolIcon::new_file);
  add_standard(open, ToolIcon::open);
  add_standard(save_action, ToolIcon::save);
  quit->setShortcut(QKeySequence::Quit);

  using lumos::qt::SourceEditor;
  QMenu* edit = menuBar()->addMenu(tr("&Edit"));
  undo_ = edit->addAction(tr("&Undo"), editor_, &SourceEditor::undo);
  undo_->setShortcut(QKeySequence::Undo);
  redo_ = edit->addAction(tr("&Redo"), editor_, &SourceEditor::redo);
  redo_->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::ControlModifier | Qt::Key_Y)});
  edit->addSeparator();
  cut_ = edit->addAction(tr("Cu&t"), editor_, &SourceEditor::cut);
  cut_->setShortcut(QKeySequence::Cut);
  copy_ = edit->addAction(tr("&Copy"), editor_, &SourceEditor::copy);
  copy_->setShortcut(QKeySequence::Copy);
  paste_ = edit->addAction(tr("&Paste"), editor_, &SourceEditor::paste);
  paste_->setShortcut(QKeySequence::Paste);
  edit->addSeparator();
  QAction* select_all = edit->addAction(tr("Select &All"), editor_, &SourceEditor::selectAll);
  select_all->setShortcut(QKeySequence::SelectAll);
  edit->addSeparator();
  QAction* find_action = edit->addAction(tr("&Find..."), this, [this] { find_bar_->open_find(); });
  find_action->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_F));
  QAction* replace_action = edit->addAction(tr("&Replace..."), this, [this] { find_bar_->open_replace(); });
  replace_action->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_H));
  edit->addSeparator();
  read_only_ = edit->addAction(tr("Read-&only"), this, [this](bool on) {
    editor_->set_read_only(on);
    update_actions();
  });
  read_only_->setCheckable(true);
  standard_bar->addSeparator();
  add_standard(undo_, ToolIcon::undo);
  add_standard(redo_, ToolIcon::redo);
  standard_bar->addSeparator();
  add_standard(find_action, ToolIcon::find);

  QMenu* view = menuBar()->addMenu(tr("&View"));
  QMenu* toolbars = view->addMenu(tr("&Toolbars"));
  QAction* status_bar = view->addAction(tr("&Status bar"));
  status_bar->setCheckable(true);
  status_bar->setChecked(settings_.value(kStatusBar, true).toBool());
  statusBar()->setVisible(status_bar->isChecked());
  connect(status_bar, &QAction::toggled, this, [this](bool on) {
    statusBar()->setVisible(on);
    settings_.setValue(kStatusBar, on);
  });
  view->addSeparator();
  const auto add_view_setting = [&](const QString& name, const QString& key, bool fallback, auto apply) {
    const bool on = settings_.value(key, fallback).toBool();
    apply(on);
    QAction* a = view->addAction(name);
    a->setCheckable(true);
    a->setChecked(on);
    connect(a, &QAction::toggled, this, [this, key, apply](bool now) {
      apply(now);
      settings_.setValue(key, now);
    });
  };
  add_view_setting(tr("Line &numbers"), kLineNumbers, editor_->line_numbers(),
                   [this](bool on) { editor_->set_line_numbers(on); });
  add_view_setting(tr("&Limit text width"), kLimitWidth, editor_->max_text_width() > 0,
                   [this](bool on) { editor_->set_max_text_width(on ? 100 : 0); });
  add_view_setting(tr("&Paper look"), kPaperLook, false, [this](bool on) { set_paper_look(on); });
  QAction* drops = view->addAction(tr("Accept &drops"));
  drops->setCheckable(true);
  drops->setChecked(editor_->acceptDrops());
  connect(drops, &QAction::toggled, editor_, &lumos::qt::SourceEditor::setAcceptDrops);
  view->addSeparator();
  view->addAction(tr("&Font..."), this, [this] { choose_font(); });
  zoom_in_ = view->addAction(tr("Zoom &in"), this, [this] { set_zoom(zoom_ + 0.1); });
  zoom_in_->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::ControlModifier | Qt::Key_Equal)});
  zoom_out_ = view->addAction(tr("Zoom &out"), this, [this] { set_zoom(zoom_ - 0.1); });
  zoom_out_->setShortcut(QKeySequence::ZoomOut);
  zoom_reset_ = view->addAction(tr("&Reset zoom"), this, [this] { set_zoom(1.0); });
  zoom_reset_->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_0));

  QMenu* insert = menuBar()->addMenu(tr("&Insert"));
  QMenu* format = menuBar()->addMenu(tr("F&ormat"));
  QToolBar* table_bar = addToolBar(tr("Table"));
  table_bar->setObjectName(QStringLiteral("table"));
  addToolBarBreak();
  QToolBar* bar = addToolBar(tr("Format"));
  bar->setObjectName(QStringLiteral("format"));
  const auto add_format = [&](QMenu* menu, const QString& name, const QKeySequence& keys, auto run) {
    QAction* a = menu->addAction(name, this, run);
    a->setShortcut(keys);
    a->setCheckable(true);
    bar->addAction(a);
    return a;
  };
  using lumos::InlineKind;
  bold_ = add_format(format, tr("&Bold"), QKeySequence::Bold, [this] { toggle(InlineKind::strong); });
  italic_ = add_format(format, tr("&Italic"), QKeySequence::Italic, [this] { toggle(InlineKind::emphasis); });
  strike_ = add_format(format, tr("&Strikethrough"),
                       QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_X),
                       [this] { toggle(InlineKind::strikethrough); });
  code_ = add_format(format, tr("&Code"), QKeySequence(Qt::ControlModifier | Qt::Key_E),
                     [this] { toggle(InlineKind::code); });
  link_ = add_format(insert, tr("&Link..."), QKeySequence(Qt::ControlModifier | Qt::Key_K), [this] { toggle_link(); });
  picture_ = insert->addAction(tr("&Picture..."), this, [this] { insert_picture(); });

  format->addSeparator();
  bar->addSeparator();
  heading_ = new QComboBox(this);
  heading_->addItem(tr("Paragraph"));
  for (int level = 1; level <= 6; ++level) heading_->addItem(tr("Heading %1").arg(level));
  heading_->setFocusPolicy(Qt::NoFocus);
  heading_->setToolTip(tr("Heading"));
  connect(heading_, &QComboBox::activated, this, [this](int level) { set_heading(level); });
  bar->insertWidget(bold_, heading_);
  bar->insertSeparator(bold_);
  QMenu* headings = format->addMenu(tr("&Heading"));
  for (int level = 0; level <= 6; ++level) {
    QAction* a = headings->addAction(level == 0 ? tr("&Paragraph") : tr("Heading &%1").arg(level), this,
                                     [this, level] {
                                       const int now = lumos::format_at(editor_->document(),
                                                                        editor_->selection()).heading_level;
                                       set_heading(level == now ? 0 : level);
                                     });
    a->setShortcut(level == 0 ? QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_N)
                              : QKeySequence(Qt::ControlModifier | static_cast<Qt::Key>(Qt::Key_0 + level)));
  }
  using lumos::BlockKind;
  const auto ctrl_shift = [](Qt::Key key) { return QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | key); };
  bullet_ = add_format(format, tr("B&ullet list"), ctrl_shift(Qt::Key_8), [this] { toggle(BlockKind::bullet_list); });
  ordered_ = add_format(format, tr("&Ordered list"), ctrl_shift(Qt::Key_7),
                        [this] { toggle(BlockKind::ordered_list); });
  task_ = add_format(format, tr("&Task list"), ctrl_shift(Qt::Key_9), [this] { toggle(BlockKind::task_list); });
  quote_ = add_format(format, tr("&Quote"), ctrl_shift(Qt::Key_Q), [this] { toggle(BlockKind::block_quote); });
  renumber_ = add_format(format, tr("&Renumber"), QKeySequence(), [this] {
    editor_->apply_command(lumos::renumber(editor_->document(), editor_->selection()));
    update_actions();
  });
  renumber_->setCheckable(false);
  bar->addSeparator();
  thematic_break_ = add_format(insert, tr("&Horizontal line"), QKeySequence(), [this] {
    editor_->apply_command(lumos::insert_thematic_break(editor_->document(), editor_->selection()));
    update_actions();
  });
  thematic_break_->setCheckable(false);
  code_block_ = add_format(insert, tr("&Code block"), QKeySequence(), [this] { toggle(BlockKind::code_block); });

  table_button_ = new QToolButton(this);
  table_button_->setText(tr("Table"));
  table_button_->setIcon(toolbar_icon(ToolIcon::table));
  table_button_->setToolTip(tr("Insert table"));
  table_button_->setPopupMode(QToolButton::InstantPopup);
  QMenu* table_menu = new QMenu(table_button_);
  auto* picker = new TableGridPicker([this, table_menu](int rows, int cols) {
    insert_table(rows, cols);
    table_menu->close();
  });
  auto* picker_action = new QWidgetAction(table_menu);
  picker_action->setDefaultWidget(picker);
  table_menu->addAction(picker_action);
  table_button_->setMenu(table_menu);
  table_bar->addWidget(table_button_);
  table_action_ = insert->addAction(tr("&Table..."), this, [this] { table_button_->showMenu(); });

  QMenu* table = menuBar()->addMenu(tr("T&able"));
  table->addAction(table_action_);
  table->addSeparator();
  const auto add_move = [&](const QString& name, auto command) {
    table_moves_ << table->addAction(name, this, [this, command] { table_move(command()); });
  };
  add_move(tr("&Next cell\tTab"),
           [this] { return lumos::next_cell(editor_->document(), editor_->selection(), editor_->newline()); });
  add_move(tr("&Previous cell\tShift+Tab"),
           [this] { return lumos::previous_cell(editor_->document(), editor_->selection()); });
  add_move(tr("Next &row\tEnter"),
           [this] { return lumos::next_row(editor_->document(), editor_->selection(), editor_->newline()); });
  add_move(tr("Previous r&ow\tShift+Enter"),
           [this] { return lumos::previous_row(editor_->document(), editor_->selection()); });
  table->addSeparator();
  format_table_ = table->addAction(tr("&Format table"), this, [this] {
    editor_->apply_command(lumos::format_table(editor_->document(), editor_->selection()));
    update_actions();
  });
  table_bar->addAction(format_table_);

  QMenu* help = menuBar()->addMenu(tr("&Help"));
  help->addAction(tr("&About lumos-md"), this, [this] { about(); });

  const std::pair<QAction*, ToolIcon> icons[] = {
      {bold_, ToolIcon::bold},          {italic_, ToolIcon::italic},
      {strike_, ToolIcon::strike},      {code_, ToolIcon::code},
      {link_, ToolIcon::link},          {bullet_, ToolIcon::bullet_list},
      {ordered_, ToolIcon::ordered_list}, {task_, ToolIcon::task_list},
      {quote_, ToolIcon::quote},        {renumber_, ToolIcon::renumber},
      {thematic_break_, ToolIcon::horizontal_line}, {code_block_, ToolIcon::code_block},
      {format_table_, ToolIcon::format_table}};
  for (const auto& [action, icon] : icons) action->setIcon(toolbar_icon(icon));
  for (QToolBar* b : {standard_bar, bar, table_bar}) {
    toolbars->addAction(b->toggleViewAction());
    for (QAction* a : b->actions()) {
      if (a->isSeparator() || a->text().isEmpty()) continue;
      const QString name = QString(a->text()).remove(QLatin1Char('&')).remove(QStringLiteral("..."));
      a->setToolTip(a->shortcut().isEmpty()
                        ? name
                        : tr("%1 (%2)").arg(name, a->shortcut().toString(QKeySequence::NativeText)));
    }
  }
  restoreState(settings_.value(kWindowState).toByteArray());

  editor_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(editor_, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) { context_menu(at); });
  block_state_ = new QLabel(this);
  statusBar()->addWidget(block_state_);
  position_label_ = new QLabel(this);
  statusBar()->addPermanentWidget(position_label_);
  words_label_ = new QLabel(this);
  statusBar()->addPermanentWidget(words_label_);
  words_timer_ = new QTimer(this);
  words_timer_->setSingleShot(true);
  words_timer_->setInterval(300);
  connect(words_timer_, &QTimer::timeout, this, [this] { update_words(); });
  zoom_label_ = new QLabel(this);
  statusBar()->addPermanentWidget(zoom_label_);
  set_zoom(zoom_);

  connect(editor_, &SourceEditor::modificationChanged, this, [this](bool on) {
    setWindowModified(on);
    revert_->setEnabled(on && !path_.isEmpty());
  });
  connect(editor_, &SourceEditor::canUndoChanged, undo_, &QAction::setEnabled);
  connect(editor_, &SourceEditor::canRedoChanged, redo_, &QAction::setEnabled);
  connect(editor_, &SourceEditor::cursorPositionChanged, this, [this] {
    update_actions();
    update_position();
  });
  connect(editor_, &SourceEditor::textChanged, this, [this] {
    update_actions();
    update_position();
    update_marks();
    words_timer_->start();
  });
  update_actions();
  update_position();
  update_words();
  update_title();
}

bool MainWindow::load(const QString& path, bool keep_position) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    lumos::log(lumos::LogLevel::debug, kComp, "open_failed",
               {{"path", path.toStdString()}, {"reason", f.errorString().toStdString()}});
    QMessageBox::warning(this, tr("Open"), tr("Cannot open %1:\n%2").arg(path, f.errorString()));
    return false;
  }
  const QByteArray bytes = f.readAll();
  editor_->set_text(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())), keep_position);
  update_marks();
  update_words();
  path_ = path;
  settings_.sync();
  set_recent_files(add_recent_file(settings_.value(kRecentFiles).toStringList(), path));
  lumos::log(lumos::LogLevel::debug, kComp, "open",
             {{"path", path.toStdString()}, {"bytes", std::to_string(bytes.size())},
              {"reload", keep_position ? "1" : "0"}});
  update_title();
  update_actions();
  return true;
}

void MainWindow::closeEvent(QCloseEvent* e) {
  if (maybe_save()) {
    settings_.setValue(kGeometry, saveGeometry());
    settings_.setValue(kWindowState, saveState());
    e->accept();
  } else {
    e->ignore();
  }
}

void MainWindow::set_paper_look(bool on) {
  paper_look_ = on;
  editor_->set_style_table(on ? paper_styles(editor_->font(), editor_->palette())
                              : lumos::qt::StyleTable::make_default(editor_->font(), editor_->palette()));
}

void MainWindow::choose_font() {
  QFont base(font_family_);
  base.setPointSizeF(font_size_);
  bool ok = false;
  const QFont f = QFontDialog::getFont(&ok, base, this, tr("Font"));
  if (!ok) return;
  font_family_ = f.family();
  if (f.pointSizeF() > 0) font_size_ = std::clamp(f.pointSizeF(), kMinFontSize, kMaxFontSize);
  settings_.setValue(kFontFamily, font_family_);
  settings_.setValue(kFontSize, font_size_);
  apply_font();
}

void MainWindow::set_zoom(double zoom) {
  zoom = round_zoom(zoom);
  if (zoom != zoom_) {
    zoom_ = zoom;
    settings_.setValue(kZoom, zoom_);
    apply_font();
  }
  zoom_in_->setEnabled(zoom_ < kMaxZoom);
  zoom_out_->setEnabled(zoom_ > kMinZoom);
  zoom_reset_->setEnabled(zoom_ != 1.0);
  zoom_label_->setText(tr("%1%").arg(std::lround(zoom_ * 100)));
}

void MainWindow::apply_font() {
  QFont f(font_family_);
  f.setPointSizeF(font_size_ * zoom_);
  editor_->setFont(f);
  if (paper_look_) set_paper_look(true);
  lumos::log(lumos::LogLevel::debug, kComp, "font",
             {{"size", QString::number(font_size_).toStdString()}, {"zoom", QString::number(zoom_).toStdString()}});
}

bool MainWindow::eventFilter(QObject* watched, QEvent* e) {
  if (watched == editor_->viewport() && e->type() == QEvent::Wheel) {
    auto* we = static_cast<QWheelEvent*>(e);
    if (we->modifiers() & Qt::ControlModifier) {
      wheel_delta_ += we->angleDelta().y();
      for (; wheel_delta_ >= 120; wheel_delta_ -= 120) set_zoom(zoom_ + 0.1);
      for (; wheel_delta_ <= -120; wheel_delta_ += 120) set_zoom(zoom_ - 0.1);
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, e);
}

void MainWindow::new_file() {
  if (!maybe_save()) return;
  editor_->set_text("");
  update_marks();
  update_words();
  path_.clear();
  lumos::log(lumos::LogLevel::debug, kComp, "new", {});
  update_title();
  update_actions();
}

void MainWindow::open_dialog() {
  if (!maybe_save()) return;
  const QString path = QFileDialog::getOpenFileName(this, tr("Open"), QString(),
                                                    tr("Markdown (*.md *.markdown *.txt);;All files (*)"));
  if (!path.isEmpty()) load(path);
}

void MainWindow::open_recent(const QString& path) {
  if (!maybe_save()) return;
  if (load(path) || QFileInfo::exists(path)) return;
  settings_.sync();
  set_recent_files(remove_recent_file(settings_.value(kRecentFiles).toStringList(), path));
}

void MainWindow::fill_recent_menu() {
  settings_.sync();
  const QStringList list = settings_.value(kRecentFiles).toStringList();
  recent_menu_->clear();
  const QString home = QDir::homePath();
  for (int i = 0; i < list.size() && i < kRecentFilesMax; ++i) {
    const QString path = list[i];
    recent_menu_->addAction(recent_file_label(i + 1, path, home), this, [this, path] { open_recent(path); });
  }
  if (!list.isEmpty()) recent_menu_->addSeparator();
  QAction* clear = recent_menu_->addAction(tr("&Clear list"), this, [this] { set_recent_files({}); });
  clear->setEnabled(!list.isEmpty());
}

void MainWindow::set_recent_files(const QStringList& list) {
  settings_.setValue(kRecentFiles, list);
  settings_.sync();
  lumos::log(lumos::LogLevel::debug, kComp, "recent_files", {{"count", std::to_string(list.size())}});
}

void MainWindow::revert() {
  if (path_.isEmpty() || !editor_->is_modified()) return;
  const auto answer =
      QMessageBox::question(this, tr("Revert"), tr("Drop the changes and load the saved file again?"),
                            QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
  if (answer == QMessageBox::Discard) load(path_, true);
}

void MainWindow::about() {
  QMessageBox::about(this, tr("About lumos-md"),
                     tr("<b>lumos-md %1</b><p>A Markdown editor.</p><p>License: GNU AGPL 3.0</p>")
                         .arg(QStringLiteral(LUMOS_MD_VERSION)));
}

void MainWindow::table_move(std::optional<lumos::CommandResult> result) {
  editor_->apply_command(std::move(result));
  update_actions();
}

void MainWindow::update_actions() {
  const bool ro = editor_->is_read_only();
  const bool sel = editor_->has_selection();
  undo_->setEnabled(editor_->canUndo());
  redo_->setEnabled(editor_->canRedo());
  cut_->setEnabled(sel && !ro);
  copy_->setEnabled(sel);
  paste_->setEnabled(!ro);

  const lumos::Format f = lumos::format_at(editor_->document(), editor_->selection());
  const std::pair<QAction*, bool> states[] = {
      {bold_, f.strong},
      {italic_, f.emphasis},
      {strike_, f.strikethrough},
      {code_, f.code},
      {link_, f.link},
      {bullet_, f.list == lumos::ListKind::bullet},
      {ordered_, f.list == lumos::ListKind::ordered},
      {task_, f.task},
      {quote_, f.block_quote},
      {code_block_, f.code_block}};
  for (const auto& [action, on] : states) {
    action->setChecked(on);
    action->setEnabled(!ro);
  }
  thematic_break_->setEnabled(!ro);
  picture_->setEnabled(!ro);
  renumber_->setEnabled(!ro);
  const bool table_ok = !ro && !f.table && !f.code_block;
  table_button_->setEnabled(table_ok);
  table_action_->setEnabled(table_ok);
  for (QAction* a : table_moves_) a->setEnabled(f.table && !ro);
  format_table_->setEnabled(f.table && !ro);
  revert_->setEnabled(editor_->is_modified() && !path_.isEmpty());
  heading_->setCurrentIndex(f.heading_level);
  heading_->setEnabled(!ro);
  QStringList block;
  if (f.heading_level > 0) block << tr("Heading %1").arg(f.heading_level);
  if (f.list == lumos::ListKind::bullet) block << tr("Bullet list");
  if (f.list == lumos::ListKind::ordered) block << tr("Ordered list");
  if (f.task) block << tr("Task");
  if (f.block_quote) block << tr("Quote");
  if (f.table) block << tr("Table");
  if (f.code_block) block << tr("Code block");
  block_state_->setText(block.isEmpty() ? tr("Paragraph") : block.join(QStringLiteral(" · ")));
}

void MainWindow::update_position() {
  const lumos::Buffer& b = editor_->document().buffer();
  const std::size_t cursor = editor_->cursor_position();
  const std::size_t line = b.line_of(cursor);
  const std::size_t start = b.line_start(line);
  const std::string before = b.slice(start, cursor - start);
  const auto column = std::count_if(before.begin(), before.end(), [](char c) { return (c & 0xC0) != 0x80; });
  position_label_->setText(tr("Ln %1, Col %2").arg(line + 1).arg(column + 1));
}

void MainWindow::update_words() {
  const auto words = static_cast<qulonglong>(lumos::properties(editor_->document()).words);
  words_label_->setText(tr("%1 words").arg(QLocale().toString(words)));
}

void MainWindow::update_marks() {
  QTextCharFormat f;
  f.setUnderlineStyle(QTextCharFormat::WaveUnderline);
  f.setUnderlineColor(Qt::red);
  editor_->set_marks(1, lumos::out_of_sequence(editor_->document()), f);
}

void MainWindow::context_menu(const QPoint& at) {
  QMenu menu(this);
  menu.addAction(undo_);
  menu.addAction(redo_);
  menu.addSeparator();
  menu.addAction(cut_);
  menu.addAction(copy_);
  menu.addAction(paste_);
  const std::size_t offset = editor_->offset_at(at);
  const lumos::Selection sel{offset, offset};
  for (const lumos::ByteRange& r : editor_->marks(1)) {
    if (offset < r.start || offset > r.end || editor_->is_read_only()) continue;
    menu.addSeparator();
    menu.addAction(tr("Renumber list"), this, [this, sel] {
      editor_->apply_command(lumos::renumber(editor_->document(), sel));
      update_actions();
    });
    menu.addAction(tr("Make it text"), this, [this, sel] {
      editor_->apply_command(lumos::marker_to_text(editor_->document(), sel));
      update_actions();
    });
    break;
  }
  menu.exec(editor_->viewport()->mapToGlobal(at));
}

void MainWindow::toggle(lumos::InlineKind kind) {
  editor_->apply_command(lumos::toggle_inline(editor_->document(), editor_->selection(), kind));
  update_actions();
}

void MainWindow::toggle(lumos::BlockKind kind) {
  editor_->apply_command(lumos::toggle_block(editor_->document(), editor_->selection(), kind));
  update_actions();
}

void MainWindow::set_heading(int level) {
  editor_->apply_command(lumos::set_heading(editor_->document(), editor_->selection(), level));
  update_actions();
}

void MainWindow::insert_table(int rows, int cols) {
  editor_->apply_command(
      lumos::insert_table(editor_->document(), editor_->selection(), rows, cols, editor_->newline()));
  update_actions();
}

void MainWindow::toggle_link() {
  const lumos::Selection sel = editor_->selection();
  if (lumos::format_at(editor_->document(), sel).link) {
    editor_->apply_command(lumos::remove_link(editor_->document(), sel));
    update_actions();
    return;
  }
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Insert link"));
  auto* form = new QFormLayout(&dialog);
  auto* text = new QLineEdit(QString::fromStdString(editor_->selected_text()).simplified(), &dialog);
  auto* url = new QLineEdit(&dialog);
  url->setPlaceholderText(QStringLiteral("https://"));
  form->addRow(tr("&Text:"), text);
  form->addRow(tr("&URL:"), url);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  form->addRow(buttons);
  (text->text().isEmpty() ? text : url)->setFocus();
  if (dialog.exec() == QDialog::Accepted) {
    editor_->apply_command(lumos::insert_link(editor_->document(), sel, text->text().toStdString(),
                                              url->text().trimmed().toStdString()));
  }
  update_actions();
}

void MainWindow::insert_picture() {
  const lumos::Selection sel = editor_->selection();
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Insert picture"));
  auto* form = new QFormLayout(&dialog);
  auto* alt = new QLineEdit(QString::fromStdString(editor_->selected_text()).simplified(), &dialog);
  auto* file = new QLineEdit(&dialog);
  auto* browse = new QPushButton(tr("&Browse..."), &dialog);
  auto* file_row = new QHBoxLayout;
  file_row->addWidget(file);
  file_row->addWidget(browse);
  form->addRow(tr("&Alt text:"), alt);
  form->addRow(tr("&File:"), file_row);
  const QDir note_dir = path_.isEmpty() ? QDir::current() : QFileInfo(path_).absoluteDir();
  connect(browse, &QPushButton::clicked, &dialog, [&] {
    const QString chosen = QFileDialog::getOpenFileName(
        &dialog, tr("Insert picture"), note_dir.path(),
        tr("Pictures (*.png *.jpg *.jpeg *.gif *.webp *.svg);;All files (*)"));
    if (chosen.isEmpty()) return;
    const QString rel = note_dir.relativeFilePath(chosen);
    file->setText(path_.isEmpty() || rel.startsWith(QStringLiteral("..")) ? chosen : rel);
    if (alt->text().isEmpty()) alt->setText(QFileInfo(chosen).completeBaseName());
  });
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  form->addRow(buttons);
  file->setFocus();
  if (dialog.exec() == QDialog::Accepted) {
    editor_->apply_command(lumos::insert_image(editor_->document(), sel, alt->text().toStdString(),
                                               file->text().trimmed().toStdString()));
  }
  update_actions();
}

void MainWindow::show_properties() {
  const lumos::Properties p = lumos::properties(editor_->document());
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Properties"));
  auto* form = new QFormLayout(&dialog);
  const auto row = [&](const QString& name, std::uint64_t n) {
    form->addRow(name, new QLabel(QLocale().toString(static_cast<qulonglong>(n)), &dialog));
  };
  row(tr("Bytes:"), p.bytes);
  row(tr("Characters:"), p.characters);
  row(tr("Words:"), p.words);
  row(tr("Lines:"), p.lines);
  row(tr("Headings:"), p.headings);
  row(tr("Paragraphs:"), p.paragraphs);
  row(tr("Links:"), p.links);
  row(tr("Images:"), p.images);
  row(tr("Code blocks:"), p.code_blocks);
  row(tr("Tables:"), p.tables);
  row(tr("Tasks done:"), p.tasks_done);
  row(tr("Tasks open:"), p.tasks_open);
  std::uint64_t local = 0;
  for (const lumos::LinkTarget& t : p.targets) {
    using enum lumos::UrlForm;
    if (t.form == relative_path || t.form == absolute_path || t.form == file_url) ++local;
  }
  row(tr("Local files:"), local);
  for (const lumos::Diagnostic& d : p.diagnostics) {
    if (d.code == lumos::DiagnosticCode::link_expansion_large) {
      form->addRow(new QLabel(tr("Reference links copy %1 bytes into HTML output.")
                                  .arg(QLocale().toString(static_cast<qulonglong>(d.value))),
                              &dialog));
    }
  }
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  form->addRow(buttons);
  dialog.exec();
}

bool MainWindow::save() {
  if (path_.isEmpty()) return save_as();
  return write(path_);
}

bool MainWindow::save_as() {
  const QString path = QFileDialog::getSaveFileName(this, tr("Save As"), path_);
  if (path.isEmpty()) return false;
  if (!write(path)) return false;
  path_ = path;
  settings_.sync();
  set_recent_files(add_recent_file(settings_.value(kRecentFiles).toStringList(), path));
  update_title();
  update_actions();
  return true;
}

bool MainWindow::write(const QString& path) {
  const std::string text = editor_->text();  
  QSaveFile f(path);
  const bool ok = f.open(QIODevice::WriteOnly) &&
                  f.write(text.data(), static_cast<qint64>(text.size())) == static_cast<qint64>(text.size()) &&
                  f.commit();
  if (!ok) {
    lumos::log(lumos::LogLevel::debug, kComp, "save_failed",
               {{"path", path.toStdString()}, {"reason", f.errorString().toStdString()}});
    QMessageBox::warning(this, tr("Save"), tr("Cannot save %1:\n%2").arg(path, f.errorString()));
    return false;
  }
  editor_->mark_saved();
  lumos::log(lumos::LogLevel::debug, kComp, "save",
             {{"path", path.toStdString()}, {"bytes", std::to_string(text.size())}});
  return true;
}

bool MainWindow::maybe_save() {
  if (!editor_->is_modified()) return true;
  const auto answer = QMessageBox::question(this, tr("Unsaved changes"), tr("Save the changes?"),
                                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
  if (answer == QMessageBox::Save) return save();
  return answer == QMessageBox::Discard;
}

void MainWindow::update_title() {
  const QString name = path_.isEmpty() ? tr("Untitled") : QFileInfo(path_).fileName();
  setWindowTitle(tr("%1[*] - lumos-md").arg(name));
  setWindowModified(editor_->is_modified());
}
