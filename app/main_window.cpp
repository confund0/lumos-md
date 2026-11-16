
#include "main_window.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
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
#include <QPointer>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
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

#include "document_tab.hpp"
#include "drafts.hpp"
#include "find_bar.hpp"
#include "open_tabs.hpp"
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
const QLatin1String kDraftPrefix("draft:");

constexpr double kMinZoom = 0.5;
constexpr double kMaxZoom = 3.0;
constexpr double kMinFontSize = 4;
constexpr double kMaxFontSize = 200;
constexpr int kTabsWriteDelayMs = 300;

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

class TabBar : public QTabBar {
 public:
  using QTabBar::QTabBar;

  void set_corner(QWidget* corner) {
    corner_ = corner;
    updateGeometry();
  }
  int overlap() const { return style()->pixelMetric(QStyle::PM_TabBarBaseOverlap, nullptr, this); }
  int own_height() const { return QTabBar::sizeHint().height(); }

 protected:
  QSize sizeHint() const override { return keep_height(QTabBar::sizeHint()); }
  QSize minimumSizeHint() const override { return keep_height(QTabBar::minimumSizeHint()); }

 private:
  QSize keep_height(QSize size) const {
    if (const QWidget* corner = corner_.data(); corner != nullptr)
      size.setHeight(std::max(size.height(), corner->layout()->sizeHint().height() + overlap()));
    return size;
  }

  QPointer<QWidget> corner_;
};

class TabCorner : public QWidget {
 public:
  TabCorner(TabBar* bar, QWidget* parent) : QWidget(parent), bar_(bar) {}

  QSize sizeHint() const override {
    QSize size = QWidget::sizeHint();
    size.setHeight(std::max(size.height(), bar_->own_height() - bar_->overlap()));
    return size;
  }

 private:
  TabBar* bar_;
};

class TabWidget : public QTabWidget {
 public:
  explicit TabWidget(QWidget* parent) : QTabWidget(parent), bar_(new TabBar(this)) { setTabBar(bar_); }

  TabBar* bar() const { return bar_; }

 private:
  TabBar* bar_;
};

}  

MainWindow::MainWindow(const QString& settings_file, const QString& open_tabs_file, const QString& drafts_dir)
    : settings_(settings_file, QSettings::IniFormat), open_tabs_file_(open_tabs_file), drafts_dir_(drafts_dir) {
  lumos::log(lumos::LogLevel::debug, kComp, "settings",
             {{"status", status_name(settings_.status())}, {"file", settings_file.toStdString()}});
  const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  const double default_size = fixed.pointSizeF() > 0 ? fixed.pointSizeF() + 2 : 12;
  font_family_ = settings_.value(kFontFamily, fixed.family()).toString();
  font_size_ = settings_.value(kFontSize, default_size).toDouble();
  if (!(font_size_ >= kMinFontSize && font_size_ <= kMaxFontSize)) font_size_ = default_size;
  zoom_ = settings_.value(kZoom, 1.0).toDouble();
  zoom_ = zoom_ >= kMinZoom && zoom_ <= kMaxZoom ? round_zoom(zoom_) : 1.0;
  line_numbers_ = settings_.value(kLineNumbers, line_numbers_).toBool();
  limit_width_ = settings_.value(kLimitWidth, limit_width_).toBool();
  paper_look_ = settings_.value(kPaperLook, paper_look_).toBool();

  auto* tab_widget = new TabWidget(this);
  tabs_ = tab_widget;
  tabs_->setDocumentMode(true);
  tabs_->setMovable(true);
  tabs_->setTabsClosable(true);
  setCentralWidget(tabs_);
  DocumentTab* first_tab = make_tab();
  tabs_->addTab(first_tab, QString());
  update_tab_text(first_tab);
  connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
    refresh_for_current_tab();
    schedule_tabs_write();
    lumos::log(lumos::LogLevel::debug, kComp, "tab_switch", {{"tabs", std::to_string(tabs_->count())}});
  });
  connect(tab_widget->bar(), &QTabBar::tabMoved, this, [this](int, int) { schedule_tabs_write(); });
  connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) { close_tab(index); });
  auto* corner = new TabCorner(tab_widget->bar(), tabs_);
  auto* corner_layout = new QHBoxLayout(corner);
  constexpr double kRightMm = 4.0;
  constexpr double kMmPerInch = 25.4;
  constexpr int kAboveBelow = 3;
  const int right = static_cast<int>(corner->logicalDpiX() * kRightMm / kMmPerInch);
  corner_layout->setContentsMargins(0, kAboveBelow, right, kAboveBelow);
  corner_layout->setSpacing(2);
  auto* new_button = new QToolButton(corner);
  new_button->setText(QStringLiteral("+"));
  new_button->setToolTip(tr("New (Ctrl+N)"));
  connect(new_button, &QToolButton::clicked, this, [this] { new_tab(); });
  corner_layout->addWidget(new_button);
  reopen_button_ = new QToolButton(corner);
  reopen_button_->setText(QString::fromUtf8("\xE2\x86\xB6"));  
  reopen_button_->setToolTip(tr("Reopen closed tab (Ctrl+Shift+T)"));
  reopen_button_->setEnabled(false);
  connect(reopen_button_, &QToolButton::clicked, this, [this] { reopen_closed_tab(); });
  corner_layout->addWidget(reopen_button_);
  tabs_->setCornerWidget(corner, Qt::TopRightCorner);
  tab_widget->bar()->set_corner(corner);
  if (!restoreGeometry(settings_.value(kGeometry).toByteArray())) resize(900, 700);

  QMenu* file = menuBar()->addMenu(tr("&File"));
  QAction* new_action = file->addAction(tr("&New"), this, [this] { new_tab(); });
  new_action->setShortcuts({QKeySequence::New, QKeySequence(Qt::ControlModifier | Qt::Key_T)});
  QAction* open = file->addAction(tr("&Open..."), this, [this] { open_dialog(); });
  open->setShortcut(QKeySequence::Open);
  recent_menu_ = file->addMenu(tr("Open &Recent"));
  connect(recent_menu_, &QMenu::aboutToShow, this, [this] { fill_recent_menu(); });
  fill_recent_menu();
  QAction* save_action = file->addAction(tr("&Save"), this, [this] { save(current_tab()); });
  save_action->setShortcut(QKeySequence::Save);
  QAction* save_as_action = file->addAction(tr("Save &As..."), this, [this] { save_as(current_tab()); });
  save_as_action->setShortcut(QKeySequence::SaveAs);
  revert_ = file->addAction(tr("Re&vert"), this, [this] { revert(); });
  file->addSeparator();
  QAction* close_action = file->addAction(tr("&Close"), this, [this] { close_tab(tabs_->currentIndex()); });
  close_action->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_W));
  reopen_action_ = file->addAction(tr("Reopen &closed tab"), this, [this] { reopen_closed_tab(); });
  reopen_action_->setShortcut(QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_T));
  reopen_action_->setEnabled(false);
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
  undo_ = edit->addAction(tr("&Undo"), this, [this] { current_tab()->editor()->undo(); });
  undo_->setShortcut(QKeySequence::Undo);
  redo_ = edit->addAction(tr("&Redo"), this, [this] { current_tab()->editor()->redo(); });
  redo_->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::ControlModifier | Qt::Key_Y)});
  edit->addSeparator();
  cut_ = edit->addAction(tr("Cu&t"), this, [this] { current_tab()->editor()->cut(); });
  cut_->setShortcut(QKeySequence::Cut);
  copy_ = edit->addAction(tr("&Copy"), this, [this] { current_tab()->editor()->copy(); });
  copy_->setShortcut(QKeySequence::Copy);
  paste_ = edit->addAction(tr("&Paste"), this, [this] { current_tab()->editor()->paste(); });
  paste_->setShortcut(QKeySequence::Paste);
  edit->addSeparator();
  QAction* select_all = edit->addAction(tr("Select &All"), this, [this] { current_tab()->editor()->selectAll(); });
  select_all->setShortcut(QKeySequence::SelectAll);
  edit->addSeparator();
  QAction* find_action = edit->addAction(tr("&Find..."), this, [this] { current_tab()->find_bar()->open_find(); });
  find_action->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_F));
  QAction* replace_action =
      edit->addAction(tr("&Replace..."), this, [this] { current_tab()->find_bar()->open_replace(); });
  replace_action->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_H));
  edit->addSeparator();
  read_only_ = edit->addAction(tr("Read-&only"), this, [this](bool on) {
    current_tab()->editor()->set_read_only(on);
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
  const auto add_view_setting = [&](const QString& name, bool current, auto on_toggle) {
    QAction* a = view->addAction(name);
    a->setCheckable(true);
    a->setChecked(current);
    connect(a, &QAction::toggled, this, on_toggle);
    return a;
  };
  add_view_setting(tr("Line &numbers"), line_numbers_, [this](bool on) {
    line_numbers_ = on;
    settings_.setValue(kLineNumbers, on);
    for_each_tab([on](DocumentTab* tab) { tab->editor()->set_line_numbers(on); });
  });
  add_view_setting(tr("&Limit text width"), limit_width_, [this](bool on) {
    limit_width_ = on;
    settings_.setValue(kLimitWidth, on);
    for_each_tab([on](DocumentTab* tab) { tab->editor()->set_max_text_width(on ? 100 : 0); });
  });
  add_view_setting(tr("&Paper look"), paper_look_, [this](bool on) {
    settings_.setValue(kPaperLook, on);
    set_paper_look(on);
  });
  QAction* drops = view->addAction(tr("Accept &drops"));
  drops->setCheckable(true);
  drops->setChecked(accept_drops_);
  connect(drops, &QAction::toggled, this, [this](bool on) {
    accept_drops_ = on;
    for_each_tab([on](DocumentTab* tab) { tab->editor()->setAcceptDrops(on); });
  });
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
                                       SourceEditor* editor = current_tab()->editor();
                                       const int now =
                                           lumos::format_at(editor->document(), editor->selection()).heading_level;
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
    SourceEditor* editor = current_tab()->editor();
    editor->apply_command(lumos::renumber(editor->document(), editor->selection()));
    update_actions();
  });
  renumber_->setCheckable(false);
  bar->addSeparator();
  thematic_break_ = add_format(insert, tr("&Horizontal line"), QKeySequence(), [this] {
    SourceEditor* editor = current_tab()->editor();
    editor->apply_command(lumos::insert_thematic_break(editor->document(), editor->selection()));
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
  add_move(tr("&Next cell\tTab"), [this] {
    SourceEditor* editor = current_tab()->editor();
    return lumos::next_cell(editor->document(), editor->selection(), editor->newline());
  });
  add_move(tr("&Previous cell\tShift+Tab"), [this] {
    SourceEditor* editor = current_tab()->editor();
    return lumos::previous_cell(editor->document(), editor->selection());
  });
  add_move(tr("Next &row\tEnter"), [this] {
    SourceEditor* editor = current_tab()->editor();
    return lumos::next_row(editor->document(), editor->selection(), editor->newline());
  });
  add_move(tr("Previous r&ow\tShift+Enter"), [this] {
    SourceEditor* editor = current_tab()->editor();
    return lumos::previous_row(editor->document(), editor->selection());
  });
  table->addSeparator();
  format_table_ = table->addAction(tr("&Format table"), this, [this] {
    SourceEditor* editor = current_tab()->editor();
    editor->apply_command(lumos::format_table(editor->document(), editor->selection()));
    update_actions();
  });
  table_bar->addAction(format_table_);

  window_menu_ = menuBar()->addMenu(tr("&Window"));
  QAction* next_tab = window_menu_->addAction(tr("&Next Tab"), this, [this] { step_tab(1); });
  next_tab->setShortcuts(
      {QKeySequence(Qt::ControlModifier | Qt::Key_Tab), QKeySequence(Qt::ControlModifier | Qt::Key_PageDown)});
  QAction* previous_tab = window_menu_->addAction(tr("&Previous Tab"), this, [this] { step_tab(-1); });
  previous_tab->setShortcuts({QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_Tab),
                              QKeySequence(Qt::ControlModifier | Qt::Key_PageUp)});
  window_menu_->addSeparator();
  connect(window_menu_, &QMenu::aboutToShow, this, [this] { fill_window_menu(); });

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

  update_actions();
  update_position();
  update_words();
  update_title();
}

bool MainWindow::open_file(const QString& path) { return open_path(path, false); }

bool MainWindow::open_path(const QString& path, bool quiet) {
  const QString canonical = QFileInfo(path).canonicalFilePath();
  if (!canonical.isEmpty()) {
    for (int i = 0; i < tabs_->count(); ++i) {
      DocumentTab* t = tab_at(i);
      if (t != nullptr && !t->path().isEmpty() && QFileInfo(t->path()).canonicalFilePath() == canonical) {
        tabs_->setCurrentIndex(i);
        return true;
      }
    }
  }
  DocumentTab* current = current_tab();
  const bool reuse = current != nullptr && current->is_blank();
  DocumentTab* tab = reuse ? current : make_tab();
  QString error;
  if (!tab->load(path, false, &error)) {
    if (!quiet) QMessageBox::warning(this, tr("Open"), tr("Cannot open %1:\n%2").arg(path, error));
    else lumos::log(lumos::LogLevel::debug, kComp, "open_quiet_failed", {{"size", std::to_string(path.size())}});
    if (!reuse) delete tab;
    return false;
  }
  if (!reuse) tabs_->insertTab(tabs_->currentIndex() + 1, tab, QString());
  update_tab_text(tab);
  settings_.sync();
  set_recent_files(add_recent_file(settings_.value(kRecentFiles).toStringList(), path));
  const int index = tabs_->indexOf(tab);
  if (tabs_->currentIndex() == index) refresh_for_current_tab();
  else tabs_->setCurrentIndex(index);
  offer_path_draft(tab);
  schedule_tabs_write();
  return true;
}

void MainWindow::open_untitled_draft(const QString& id) {
  std::unique_ptr<QLockFile> lock = lock_draft(drafts_dir_, id);
  if (lock == nullptr) {
    lumos::log(lumos::LogLevel::debug, kComp, "draft_skip", {{"id", id.toStdString()}, {"reason", "locked"}});
    return;
  }
  const DraftRead read = read_draft(drafts_dir_, id);
  if (read.state == DraftState::missing) {
    lumos::log(lumos::LogLevel::debug, kComp, "draft_skip", {{"id", id.toStdString()}, {"reason", "missing"}});
    return;
  }
  const QByteArray& bytes = read.state == DraftState::unreadable ? read.bytes : read.draft.text;
  DocumentTab* current = current_tab();
  const bool reuse = current != nullptr && current->is_blank();
  DocumentTab* tab = reuse ? current : make_tab();
  tab->editor()->replace_text(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
  if (!reuse) tabs_->insertTab(tabs_->currentIndex() + 1, tab, QString());
  update_tab_text(tab);
  tab->adopt_draft(id, std::move(lock));
  const int index = tabs_->indexOf(tab);
  if (tabs_->currentIndex() == index) refresh_for_current_tab();
  else tabs_->setCurrentIndex(index);
  const char* state = read.state == DraftState::ok ? "ok" : read.state == DraftState::damaged ? "damaged" : "unreadable";
  lumos::log(lumos::LogLevel::debug, kComp, "draft_restore", {{"id", id.toStdString()}, {"state", state}});
  if (read.state != DraftState::ok)
    statusBar()->showMessage(tr("A draft was damaged; its text may be incomplete."), 10000);
}

void MainWindow::offer_path_draft(DocumentTab* tab) {
  if (drafts_dir_.isEmpty()) return;
  QString id;
  std::unique_ptr<QLockFile> lock;
  for (const QString& candidate : path_draft_ids(drafts_dir_, tab->path())) {
    lock = lock_draft(drafts_dir_, candidate);
    if (lock != nullptr) {
      id = candidate;
      break;
    }
    lumos::log(lumos::LogLevel::debug, kComp, "draft_skip", {{"id", candidate.toStdString()}, {"reason", "locked"}});
  }
  if (id.isEmpty()) return;

  const DraftRead read = read_draft(drafts_dir_, id);
  if (read.state == DraftState::missing) {
    lock.reset();
    return;
  }
  if (read.state == DraftState::ok && read.draft.text == QByteArray::fromStdString(tab->editor()->text())) {
    remove_draft(drafts_dir_, id);
    lock.reset();
    lumos::log(lumos::LogLevel::debug, kComp, "draft_same", {{"id", id.toStdString()}});
    return;
  }

  QMessageBox box(QMessageBox::Question, tr("Recover"),
                  tr("%1 has unsaved changes from %2 that were not saved (a draft).")
                      .arg(tab->name(), QLocale().toString(read.written, QLocale::ShortFormat)),
                  QMessageBox::NoButton, this);
  QStringList info;
  if (read.state != DraftState::ok) info << tr("The draft is damaged; its text may be incomplete.");
  if (read.state != DraftState::unreadable && read.draft.saved_sha256 != tab->saved_sha256())
    info << tr("The file changed on disk after the draft was written.");
  box.setInformativeText(info.join(QLatin1Char('\n')));
  QPushButton* recover = box.addButton(tr("Recover"), QMessageBox::AcceptRole);
  box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
  box.setDefaultButton(recover);
  box.setEscapeButton(recover);
  box.exec();

  const char* state_name = read.state == DraftState::ok ? "ok" : read.state == DraftState::damaged ? "damaged" : "unreadable";
  if (box.clickedButton() == recover) {
    const QByteArray& bytes = read.state == DraftState::unreadable ? read.bytes : read.draft.text;
    tab->adopt_draft(id, std::move(lock));
    tab->editor()->replace_text(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    lumos::log(lumos::LogLevel::debug, kComp, "draft_recover", {{"id", id.toStdString()}, {"state", state_name}});
  } else {
    remove_draft(drafts_dir_, id);
    lock.reset();
    lumos::log(lumos::LogLevel::debug, kComp, "draft_discard", {{"id", id.toStdString()}});
  }
}

void MainWindow::new_tab() {
  DocumentTab* tab = make_tab();
  const int index = tabs_->currentIndex() + 1;
  tabs_->insertTab(index, tab, QString());
  update_tab_text(tab);
  lumos::log(lumos::LogLevel::debug, kComp, "new", {});
  tabs_->setCurrentIndex(index);
}

void MainWindow::restore_tabs() {
  if (open_tabs_file_.isEmpty()) return;
  const OpenTabsRead read = read_open_tabs(open_tabs_file_);
  for (const QString& entry : read.paths) {
    const QString id = draft_entry_id(entry);
    if (id.isEmpty()) open_path(entry, true);
    else open_untitled_draft(id);
  }
  if (!drafts_dir_.isEmpty()) {
    for (const QString& id : list_draft_ids(drafts_dir_)) {
      if (!id.startsWith(QLatin1String("u-"))) continue;
      bool held = false;
      for (int i = 0; i < tabs_->count() && !held; ++i) {
        DocumentTab* t = tab_at(i);
        held = t != nullptr && t->draft_id() == id;
      }
      if (!held) open_untitled_draft(id);
    }
    for (const QString& id : list_draft_ids(drafts_dir_)) {
      if (!id.startsWith(QLatin1String("p-"))) continue;
      bool held = false;
      for (int i = 0; i < tabs_->count() && !held; ++i) {
        DocumentTab* t = tab_at(i);
        held = t != nullptr && t->draft_id() == id;
      }
      if (held) continue;
      std::unique_ptr<QLockFile> lock = lock_draft(drafts_dir_, id);
      if (lock == nullptr) continue;
      const DraftRead draft_read = read_draft(drafts_dir_, id);
      lock.reset();
      if (draft_read.state == DraftState::missing) continue;

      const bool has_path = draft_read.state != DraftState::unreadable && !draft_read.draft.untitled;
      if (has_path && QFileInfo(draft_read.draft.path).isReadable()) {
        bool path_open = false;
        for (int i = 0; i < tabs_->count() && !path_open; ++i) {
          DocumentTab* t = tab_at(i);
          path_open = t != nullptr && !t->path().isEmpty() &&
                      QFileInfo(t->path()).canonicalFilePath() == QFileInfo(draft_read.draft.path).canonicalFilePath();
        }
        if (path_open) {
          lumos::log(lumos::LogLevel::debug, kComp, "draft_skip", {{"id", id.toStdString()}, {"reason", "open"}});
          continue;
        }
        open_path(draft_read.draft.path, true);
        continue;
      }
      open_untitled_draft(id);
      if (has_path) statusBar()->showMessage(tr("The file of a draft is gone; its text opened as an Untitled tab."), 10000);
    }
  }
  if (read.active.isEmpty()) return;
  const QString active_draft_id = draft_entry_id(read.active);
  for (int i = 0; i < tabs_->count(); ++i) {
    DocumentTab* t = tab_at(i);
    if (t == nullptr) continue;
    const bool match = active_draft_id.isEmpty() ? t->path() == read.active : t->draft_id() == active_draft_id;
    if (match) {
      tabs_->setCurrentIndex(i);
      return;
    }
  }
}

void MainWindow::open_from_other_start(const QStringList& paths, const QString& activation_token) {
  other_start_queue_.append(paths);
  if (!activation_token.isEmpty()) other_start_token_ = activation_token;
  if (QApplication::activeModalWidget() != nullptr)
    lumos::log(lumos::LogLevel::debug, kComp, "instance_queued", {{"paths", std::to_string(paths.size())}});
  schedule_other_start_drain(0);
}

void MainWindow::schedule_other_start_drain(int delay_ms) {
  if (other_start_retry_pending_) return;
  other_start_retry_pending_ = true;
  QTimer::singleShot(delay_ms, this, [this] {
    other_start_retry_pending_ = false;
    drain_other_start_queue();
  });
}

void MainWindow::drain_other_start_queue() {
  if (other_start_draining_) return;
  if (QApplication::activeModalWidget() != nullptr) {
    schedule_other_start_drain(200);
    return;
  }
  other_start_draining_ = true;
  while (!other_start_queue_.isEmpty()) open_path(other_start_queue_.takeFirst(), false);
  other_start_draining_ = false;
  if (isMinimized()) showNormal();
  raise();
  if (other_start_token_.isEmpty())
    qunsetenv("XDG_ACTIVATION_TOKEN");
  else
    qputenv("XDG_ACTIVATION_TOKEN", other_start_token_.toUtf8());
  activateWindow();
  qunsetenv("XDG_ACTIVATION_TOKEN");
  other_start_token_.clear();
}

bool MainWindow::close_tab(int index) {
  DocumentTab* tab = tab_at(index);
  if (tab == nullptr || !maybe_save(tab)) return false;
  if (!tab->path().isEmpty()) {
    closed_tabs_ = add_recent_file(closed_tabs_, tab->path(), 10);
    update_closed_tabs_actions();
  }
  tab->drop_draft();
  tabs_->removeTab(index);
  delete tab;
  if (tabs_->count() == 0) {
    DocumentTab* blank = make_tab();
    tabs_->addTab(blank, QString());
    update_tab_text(blank);
  }
  schedule_tabs_write();
  lumos::log(lumos::LogLevel::debug, kComp, "tab_close", {{"tabs", std::to_string(tabs_->count())}});
  return true;
}

void MainWindow::reopen_closed_tab() {
  while (!closed_tabs_.isEmpty()) {
    const QString path = closed_tabs_.takeFirst();
    update_closed_tabs_actions();
    if (open_path(path, true)) return;
    lumos::log(lumos::LogLevel::debug, kComp, "reopen_closed_skip", {{"size", std::to_string(path.size())}});
  }
}

void MainWindow::update_closed_tabs_actions() {
  const bool has = !closed_tabs_.isEmpty();
  reopen_action_->setEnabled(has);
  reopen_button_->setEnabled(has);
}

void MainWindow::step_tab(int step) {
  const int count = tabs_->count();
  if (count == 0) return;
  const int index = ((tabs_->currentIndex() + step) % count + count) % count;
  tabs_->setCurrentIndex(index);
}

int MainWindow::tab_count() const { return tabs_->count(); }

DocumentTab* MainWindow::tab_at(int index) const {
  return index >= 0 && index < tabs_->count() ? static_cast<DocumentTab*>(tabs_->widget(index)) : nullptr;
}

DocumentTab* MainWindow::current_tab() const { return static_cast<DocumentTab*>(tabs_->currentWidget()); }

void MainWindow::closeEvent(QCloseEvent* e) {
  for (int i = 0; i < tabs_->count(); ++i) {
    if (!maybe_save(tab_at(i))) {
      e->ignore();
      return;
    }
  }
  for (int i = 0; i < tabs_->count(); ++i) tab_at(i)->drop_draft();
  write_tabs_now();
  tabs_closed_ = true;
  settings_.setValue(kGeometry, saveGeometry());
  settings_.setValue(kWindowState, saveState());
  e->accept();
}

void MainWindow::schedule_tabs_write() {
  if (open_tabs_file_.isEmpty() || tabs_closed_ || tabs_write_pending_) return;
  tabs_write_pending_ = true;
  QTimer::singleShot(kTabsWriteDelayMs, this, [this] {
    tabs_write_pending_ = false;
    if (!tabs_closed_) write_tabs_now();
  });
}

void MainWindow::write_tabs_now() {
  if (open_tabs_file_.isEmpty()) return;
  QStringList paths;
  QString active;
  for (int i = 0; i < tabs_->count(); ++i) {
    DocumentTab* t = tab_at(i);
    if (t == nullptr) continue;
    QString entry;
    if (!t->path().isEmpty()) entry = t->path();
    else if (!t->draft_id().isEmpty()) entry = kDraftPrefix + t->draft_id();
    else continue;
    paths << entry;
    if (t == current_tab()) active = entry;
  }
  write_open_tabs(open_tabs_file_, paths, active);
}

void MainWindow::for_each_tab(const std::function<void(DocumentTab*)>& fn) {
  for (int i = 0; i < tabs_->count(); ++i) fn(tab_at(i));
}

QFont MainWindow::current_font() const {
  QFont f(font_family_);
  f.setPointSizeF(font_size_ * zoom_);
  return f;
}

DocumentTab* MainWindow::make_tab() {
  auto* tab = new DocumentTab(tabs_);
  using lumos::qt::SourceEditor;
  SourceEditor* editor = tab->editor();
  editor->set_line_numbers(line_numbers_);
  editor->set_max_text_width(limit_width_ ? 100 : 0);
  editor->setAcceptDrops(accept_drops_);
  editor->setFont(current_font());
  if (paper_look_) editor->set_style_table(paper_styles(editor->font(), editor->palette()));
  editor->viewport()->installEventFilter(this);
  editor->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(editor, &QWidget::customContextMenuRequested, this, [this, tab](const QPoint& at) { context_menu(tab, at); });
  tab->set_link_open_failed(
      [this](const QString& url) { statusBar()->showMessage(tr("Cannot open %1").arg(url), 5000); });
  tab->set_drafts_dir(drafts_dir_);
  tab->set_draft_changed([this] { schedule_tabs_write(); });
  tab->set_draft_failed([this] { statusBar()->showMessage(tr("Cannot write the draft file."), 5000); });
  connect(editor, &SourceEditor::modificationChanged, this, [this, tab](bool on) {
    update_tab_text(tab);
    if (tab == current_tab()) {
      setWindowModified(on);
      revert_->setEnabled(on && !tab->path().isEmpty());
    }
  });
  connect(editor, &SourceEditor::textChanged, this, [this, tab] {
    if (tab == current_tab()) {
      update_actions();
      update_position();
      words_timer_->start();
    }
  });
  connect(editor, &SourceEditor::cursorPositionChanged, this, [this, tab] {
    if (tab == current_tab()) {
      update_actions();
      update_position();
    }
  });
  connect(editor, &SourceEditor::canUndoChanged, this, [this, tab](bool can) {
    if (tab == current_tab()) undo_->setEnabled(can);
  });
  connect(editor, &SourceEditor::canRedoChanged, this, [this, tab](bool can) {
    if (tab == current_tab()) redo_->setEnabled(can);
  });
  return tab;
}

void MainWindow::update_tab_text(DocumentTab* tab) {
  const int index = tabs_->indexOf(tab);
  if (index < 0) return;
  QString text = QString(tab->name()).replace(QLatin1Char('&'), QStringLiteral("&&"));
  if (tab->editor()->is_modified()) text += QStringLiteral(" *");
  tabs_->setTabText(index, text);
  tabs_->setTabToolTip(index, tab->path().isEmpty() ? tab->name() : tab->path());
}

void MainWindow::refresh_for_current_tab() {
  DocumentTab* tab = current_tab();
  if (tab == nullptr) return;
  update_actions();
  update_position();
  update_words();
  update_title();
  read_only_->setChecked(tab->editor()->is_read_only());
  undo_->setEnabled(tab->editor()->canUndo());
  redo_->setEnabled(tab->editor()->canRedo());
  tab->editor()->setFocus();
}

void MainWindow::fill_window_menu() {
  for (QAction* a : window_tab_actions_) {
    window_menu_->removeAction(a);
    delete a;
  }
  window_tab_actions_.clear();
  for (int i = 0; i < tabs_->count(); ++i) {
    DocumentTab* tab = tab_at(i);
    QAction* a = window_menu_->addAction(QString(tab->name()).replace(QLatin1Char('&'), QStringLiteral("&&")));
    a->setCheckable(true);
    a->setChecked(i == tabs_->currentIndex());
    connect(a, &QAction::triggered, this, [this, i] { tabs_->setCurrentIndex(i); });
    window_tab_actions_ << a;
  }
}

void MainWindow::set_paper_look(bool on) {
  paper_look_ = on;
  for_each_tab([on](DocumentTab* tab) {
    tab->editor()->set_style_table(on ? paper_styles(tab->editor()->font(), tab->editor()->palette())
                                      : lumos::qt::StyleTable::make_default(tab->editor()->font(), tab->editor()->palette()));
  });
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
  const QFont f = current_font();
  for_each_tab([&f](DocumentTab* tab) { tab->editor()->setFont(f); });
  if (paper_look_) set_paper_look(true);
  lumos::log(lumos::LogLevel::debug, kComp, "font",
             {{"size", QString::number(font_size_).toStdString()}, {"zoom", QString::number(zoom_).toStdString()}});
}

bool MainWindow::eventFilter(QObject* watched, QEvent* e) {
  if (e->type() == QEvent::Wheel) {
    bool is_tab_viewport = false;
    for (int i = 0; i < tabs_->count() && !is_tab_viewport; ++i) {
      DocumentTab* t = tab_at(i);
      if (t != nullptr) is_tab_viewport = watched == t->editor()->viewport();
    }
    auto* we = static_cast<QWheelEvent*>(e);
    if (is_tab_viewport && (we->modifiers() & Qt::ControlModifier)) {
      wheel_delta_ += we->angleDelta().y();
      for (; wheel_delta_ >= 120; wheel_delta_ -= 120) set_zoom(zoom_ + 0.1);
      for (; wheel_delta_ <= -120; wheel_delta_ += 120) set_zoom(zoom_ - 0.1);
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, e);
}

void MainWindow::open_dialog() {
  const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Open"), QString(),
                                                          tr("Markdown (*.md *.markdown *.txt);;All files (*)"));
  for (const QString& path : paths) open_file(path);
}

void MainWindow::open_recent(const QString& path) {
  if (open_file(path) || QFileInfo::exists(path)) return;
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
  DocumentTab* tab = current_tab();
  if (tab->path().isEmpty() || !tab->editor()->is_modified()) return;
  const auto answer =
      QMessageBox::question(this, tr("Revert"), tr("Drop the changes and load the saved file again?"),
                            QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
  if (answer != QMessageBox::Discard) return;
  QString error;
  if (!tab->load(tab->path(), true, &error)) {
    QMessageBox::warning(this, tr("Open"), tr("Cannot open %1:\n%2").arg(tab->path(), error));
    return;
  }
  update_tab_text(tab);
  update_title();
  update_actions();
  update_words();
}

void MainWindow::about() {
  QMessageBox::about(this, tr("About lumos-md"),
                     tr("<b>lumos-md %1</b><p>A Markdown editor.</p><p>License: GNU AGPL 3.0</p>")
                         .arg(QStringLiteral(LUMOS_MD_VERSION)));
}

void MainWindow::table_move(std::optional<lumos::CommandResult> result) {
  current_tab()->editor()->apply_command(std::move(result));
  update_actions();
}

void MainWindow::update_actions() {
  DocumentTab* tab = current_tab();
  lumos::qt::SourceEditor* editor = tab->editor();
  const bool ro = editor->is_read_only();
  const bool sel = editor->has_selection();
  undo_->setEnabled(editor->canUndo());
  redo_->setEnabled(editor->canRedo());
  cut_->setEnabled(sel && !ro);
  copy_->setEnabled(sel);
  paste_->setEnabled(!ro);

  const lumos::Format f = lumos::format_at(editor->document(), editor->selection());
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
  revert_->setEnabled(editor->is_modified() && !tab->path().isEmpty());
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
  lumos::qt::SourceEditor* editor = current_tab()->editor();
  const lumos::Buffer& b = editor->document().buffer();
  const std::size_t cursor = editor->cursor_position();
  const std::size_t line = b.line_of(cursor);
  const std::size_t start = b.line_start(line);
  const std::string before = b.slice(start, cursor - start);
  const auto column = std::count_if(before.begin(), before.end(), [](char c) { return (c & 0xC0) != 0x80; });
  position_label_->setText(tr("Ln %1, Col %2").arg(line + 1).arg(column + 1));
}

void MainWindow::update_words() {
  const auto words = static_cast<qulonglong>(lumos::properties(current_tab()->editor()->document()).words);
  words_label_->setText(tr("%1 words").arg(QLocale().toString(words)));
}

void MainWindow::context_menu(DocumentTab* tab, const QPoint& at) {
  lumos::qt::SourceEditor* editor = tab->editor();
  QMenu menu(this);
  menu.addAction(undo_);
  menu.addAction(redo_);
  menu.addSeparator();
  menu.addAction(cut_);
  menu.addAction(copy_);
  menu.addAction(paste_);
  const std::size_t offset = editor->offset_at(at);
  const lumos::Selection sel{offset, offset};
  for (const lumos::ByteRange& r : editor->marks(1)) {
    if (offset < r.start || offset > r.end || editor->is_read_only()) continue;
    menu.addSeparator();
    menu.addAction(tr("Renumber list"), this, [editor, sel] {
      editor->apply_command(lumos::renumber(editor->document(), sel));
    });
    menu.addAction(tr("Make it text"), this, [editor, sel] {
      editor->apply_command(lumos::marker_to_text(editor->document(), sel));
    });
    break;
  }
  menu.exec(editor->viewport()->mapToGlobal(at));
  update_actions();
}

void MainWindow::toggle(lumos::InlineKind kind) {
  lumos::qt::SourceEditor* editor = current_tab()->editor();
  editor->apply_command(lumos::toggle_inline(editor->document(), editor->selection(), kind));
  update_actions();
}

void MainWindow::toggle(lumos::BlockKind kind) {
  lumos::qt::SourceEditor* editor = current_tab()->editor();
  editor->apply_command(lumos::toggle_block(editor->document(), editor->selection(), kind));
  update_actions();
}

void MainWindow::set_heading(int level) {
  lumos::qt::SourceEditor* editor = current_tab()->editor();
  editor->apply_command(lumos::set_heading(editor->document(), editor->selection(), level));
  update_actions();
}

void MainWindow::insert_table(int rows, int cols) {
  lumos::qt::SourceEditor* editor = current_tab()->editor();
  editor->apply_command(lumos::insert_table(editor->document(), editor->selection(), rows, cols, editor->newline()));
  update_actions();
}

void MainWindow::toggle_link() {
  lumos::qt::SourceEditor* editor = current_tab()->editor();
  const lumos::Selection sel = editor->selection();
  if (lumos::format_at(editor->document(), sel).link) {
    editor->apply_command(lumos::remove_link(editor->document(), sel));
    update_actions();
    return;
  }
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Insert link"));
  auto* form = new QFormLayout(&dialog);
  auto* text = new QLineEdit(QString::fromStdString(editor->selected_text()).simplified(), &dialog);
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
    editor->apply_command(lumos::insert_link(editor->document(), sel, text->text().toStdString(),
                                              url->text().trimmed().toStdString()));
  }
  update_actions();
}

void MainWindow::insert_picture() {
  DocumentTab* tab = current_tab();
  lumos::qt::SourceEditor* editor = tab->editor();
  const lumos::Selection sel = editor->selection();
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Insert picture"));
  auto* form = new QFormLayout(&dialog);
  auto* alt = new QLineEdit(QString::fromStdString(editor->selected_text()).simplified(), &dialog);
  auto* file = new QLineEdit(&dialog);
  auto* browse = new QPushButton(tr("&Browse..."), &dialog);
  auto* file_row = new QHBoxLayout;
  file_row->addWidget(file);
  file_row->addWidget(browse);
  form->addRow(tr("&Alt text:"), alt);
  form->addRow(tr("&File:"), file_row);
  const QDir note_dir = tab->path().isEmpty() ? QDir::current() : QFileInfo(tab->path()).absoluteDir();
  connect(browse, &QPushButton::clicked, &dialog, [&] {
    const QString chosen = QFileDialog::getOpenFileName(
        &dialog, tr("Insert picture"), note_dir.path(),
        tr("Pictures (*.png *.jpg *.jpeg *.gif *.webp *.svg);;All files (*)"));
    if (chosen.isEmpty()) return;
    const QString rel = note_dir.relativeFilePath(chosen);
    file->setText(tab->path().isEmpty() || rel.startsWith(QStringLiteral("..")) ? chosen : rel);
    if (alt->text().isEmpty()) alt->setText(QFileInfo(chosen).completeBaseName());
  });
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  form->addRow(buttons);
  file->setFocus();
  if (dialog.exec() == QDialog::Accepted) {
    editor->apply_command(lumos::insert_image(editor->document(), sel, alt->text().toStdString(),
                                              file->text().trimmed().toStdString()));
  }
  update_actions();
}

void MainWindow::show_properties() {
  const lumos::Properties p = lumos::properties(current_tab()->editor()->document());
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

bool MainWindow::save(DocumentTab* tab) {
  if (tab->path().isEmpty()) return save_as(tab);
  return write_tab(tab, tab->path());
}

bool MainWindow::save_as(DocumentTab* tab) {
  const QString path = QFileDialog::getSaveFileName(this, tr("Save As"), tab->path());
  if (path.isEmpty()) return false;
  if (!write_tab(tab, path)) return false;
  tab->set_path(path);
  schedule_tabs_write();
  settings_.sync();
  set_recent_files(add_recent_file(settings_.value(kRecentFiles).toStringList(), path));
  update_tab_text(tab);
  if (tab == current_tab()) {
    update_title();
    update_actions();
  }
  return true;
}

bool MainWindow::write_tab(DocumentTab* tab, const QString& path) {
  QString error;
  if (!tab->write(path, &error)) {
    QMessageBox::warning(this, tr("Save"), tr("Cannot save %1:\n%2").arg(path, error));
    return false;
  }
  update_tab_text(tab);
  if (tab == current_tab()) update_title();
  return true;
}

bool MainWindow::maybe_save(DocumentTab* tab) {
  if (!tab->editor()->is_modified()) return true;
  tabs_->setCurrentWidget(tab);
  const auto answer = QMessageBox::question(this, tr("Unsaved changes"), tr("Save the changes to %1?").arg(tab->name()),
                                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
  if (answer == QMessageBox::Save) return save(tab);
  return answer == QMessageBox::Discard;
}

void MainWindow::update_title() {
  DocumentTab* tab = current_tab();
  setWindowTitle(tr("%1[*] - lumos-md").arg(tab->name()));
  setWindowModified(tab->editor()->is_modified());
}
