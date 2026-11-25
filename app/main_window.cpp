
#include "main_window.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFrame>
#include <QFontDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
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
#include "file_watch.hpp"
#include "file_types.hpp"
#include "find_bar.hpp"
#include "html_paste.hpp"
#include "line_merge.hpp"
#include "mcp_access.hpp"
#include "mcp_bridge.hpp"
#include "mcp_server.hpp"
#include "merge_window.hpp"
#include "open_tabs.hpp"
#include "preview.hpp"
#include "recent_files.hpp"
#include "theme.hpp"
#include "toolbar_icons.hpp"
#include "lumos/commands.hpp"
#include "lumos/log.hpp"
#include "lumos/qt/source_editor.hpp"
#include "lumos/tools.hpp"

namespace {

constexpr std::string_view kComp = "app";
QString help_dir() {
  return QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/" LUMOS_MD_HELP_DIR));
}

const QString kGeometry = QStringLiteral("window/geometry");
const QString kWindowState = QStringLiteral("window/state");
const QString kStatusBar = QStringLiteral("view/status_bar");
const QString kLineNumbers = QStringLiteral("view/line_numbers");
const QString kLimitWidth = QStringLiteral("view/limit_text_width");
const QString kPaperLook = QStringLiteral("view/paper_look");
const QString kTheme = QStringLiteral("view/theme");
const QString kFontFamily = QStringLiteral("view/font_family");
const QString kFontSize = QStringLiteral("view/font_size");
const QString kZoom = QStringLiteral("view/zoom");
const QString kPreview = QStringLiteral("view/preview");
const QString kPreviewShare = QStringLiteral("view/preview_share");
const QString kRecentFiles = QStringLiteral("recent/files");
const QString kMcpEnabled = QStringLiteral("mcp/enabled");
const QString kMcpSocketDir = QStringLiteral("mcp/socket_dir");
const QString kMcpUseSocketDir = QStringLiteral("mcp/use_socket_dir");
const QString kMcpAllowedFolders = QStringLiteral("mcp/allowed_folders");
const QLatin1String kDraftPrefix("draft:");

constexpr double kMinZoom = 0.5;
constexpr double kMaxZoom = 3.0;
constexpr double kMinFontSize = 4;
constexpr double kMaxFontSize = 200;
constexpr int kTabsWriteDelayMs = 300;
constexpr int kPreviewDelayMs = 300;

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
  auto ch = [t](int x, int y) { return static_cast<int>(std::lround(x + (y - x) * t)); };
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

bool paper_look_fits(const QPalette& palette) {
  return palette.color(QPalette::Active, QPalette::Base).lightness() > 127;
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

class ElidedLabel : public QLabel {
 public:
  using QLabel::QLabel;

 protected:
  QSize minimumSizeHint() const override { return {0, QLabel::minimumSizeHint().height()}; }
  QSize sizeHint() const override { return {0, QLabel::sizeHint().height()}; }
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    const QRect r = contentsRect();
    p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(text(), Qt::ElideMiddle, r.width()));
  }
};

class StatusIcon : public QWidget {
 public:
  StatusIcon(ToolIcon kind, QWidget* parent, const QColor& fixed_color = QColor())
      : QWidget(parent), icon_(fixed_color.isValid() ? toolbar_icon(kind, fixed_color) : toolbar_icon(kind)) {
    setFixedSize(16, 16);
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    icon_.paint(&p, rect());
  }

 private:
  QIcon icon_;
};

QFrame* status_section(ToolIcon kind, QLabel* label, const QString& tip, QWidget* parent,
                        const QColor& warn_color = QColor()) {
  auto* frame = new QFrame(parent);
  frame->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  frame->setToolTip(tip);
  if (warn_color.isValid()) {
    frame->setAutoFillBackground(true);
    QPalette pal = frame->palette();
    pal.setColor(QPalette::Window, warn_color);
    pal.setColor(QPalette::WindowText, Qt::black);
    frame->setPalette(pal);
    label->setPalette(pal);
  }
  auto* layout = new QHBoxLayout(frame);
  layout->setContentsMargins(4, 1, 6, 1);
  layout->setSpacing(4);
  layout->addWidget(new StatusIcon(kind, frame, warn_color.isValid() ? QColor(Qt::black) : QColor()));
  layout->addWidget(label, 1);
  return frame;
}

class TabBar : public QTabBar {
 public:
  using QTabBar::QTabBar;

  void set_corner(QWidget* corner) {
    corner_ = corner;
    updateGeometry();
  }
  int base_height() const { return style()->pixelMetric(QStyle::PM_TabBarBaseHeight, nullptr, parentWidget()); }
  int own_height() const { return QTabBar::sizeHint().height(); }

 protected:
  QSize sizeHint() const override { return keep_height(QTabBar::sizeHint()); }
  QSize minimumSizeHint() const override { return keep_height(QTabBar::minimumSizeHint()); }

  void tabInserted(int index) override {
    QTabBar::tabInserted(index);
    auto* close = new QToolButton(this);
    close->setObjectName(QStringLiteral("tab_close"));
    close->setText(QString::fromUtf8("\xC3\x97"));  
    close->setToolTip(QTabBar::tr("Close"));
    close->setFocusPolicy(Qt::NoFocus);
    connect(close, &QToolButton::clicked, this, [this, close] {
      for (int i = 0; i < count(); ++i)
        if (tabButton(i, QTabBar::RightSide) == close) {
          emit tabCloseRequested(i);
          return;
        }
    });
    setTabButton(index, QTabBar::RightSide, close);
  }

 private:
  QSize keep_height(QSize size) const {
    if (const QWidget* corner = corner_.data(); corner != nullptr)
      size.setHeight(std::max(size.height(), corner->layout()->sizeHint().height() + base_height()));
    return size;
  }

  QPointer<QWidget> corner_;
};

class TabCorner : public QWidget {
 public:
  TabCorner(TabBar* bar, QWidget* parent) : QWidget(parent), bar_(bar) {}

  QSize sizeHint() const override {
    QSize size = QWidget::sizeHint();
    size.setHeight(std::max(size.height(), bar_->own_height() - bar_->base_height()));
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

QString path_key(const QString& path) {
  const QFileInfo info(path);
  const QString canonical = info.canonicalFilePath();
  return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

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
  const Theme& theme = find_theme(settings_.value(kTheme).toString());
  theme_ = QString::fromLatin1(theme.id.data(), static_cast<qsizetype>(theme.id.size()));
  apply_theme(theme);
  mcp_enabled_ = settings_.value(kMcpEnabled, mcp_enabled_).toBool();
  mcp_socket_dir_ = settings_.value(kMcpSocketDir).toString();
  mcp_use_socket_dir_ = settings_.value(kMcpUseSocketDir, false).toBool() && !mcp_socket_dir_.isEmpty();
  mcp_allowed_folders_ = settings_.value(kMcpAllowedFolders).toStringList();

  auto* tab_widget = new TabWidget(this);
  tabs_ = tab_widget;
  tabs_->setDocumentMode(true);
  tabs_->setMovable(true);
  preview_ = new PreviewView(this);
  preview_->set_link_clicked([this](const lumos::LinkTarget& target) { current_tab()->open_link(target); });
  preview_box_ = new QWidget(this);
  auto* box_layout = new QVBoxLayout(preview_box_);
  box_layout->setSpacing(0);
  box_layout->addWidget(preview_);
  splitter_ = new PreviewSplitter(tabs_, preview_box_, this);
  splitter_->set_preview_share(settings_.value(kPreviewShare, 0.5).toDouble());
  splitter_->set_changed([this] { on_preview_size_changed(); });
  preview_box_->hide();
  preview_->hide();
  setCentralWidget(splitter_);
  DocumentTab* first_tab = make_tab();
  tabs_->addTab(first_tab, QString());
  update_tab_text(first_tab);
  connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
    refresh_for_current_tab();
    schedule_tabs_write();
    render_preview();
    watch_timer_->start();
    lumos::log(lumos::LogLevel::debug, kComp, "tab_switch", {{"tabs", std::to_string(tabs_->count())}});
  });
  file_watcher_ = new QFileSystemWatcher(this);
  watch_timer_ = new QTimer(this);
  watch_timer_->setSingleShot(true);
  watch_timer_->setInterval(200);
  connect(watch_timer_, &QTimer::timeout, this, [this] { check_files(); });
  connect(file_watcher_, &QFileSystemWatcher::fileChanged, this, [this](const QString&) { watch_timer_->start(); });
  connect(file_watcher_, &QFileSystemWatcher::directoryChanged, this,
          [this](const QString&) { watch_timer_->start(); });
  connect(tab_widget->bar(), &QTabBar::tabMoved, this, [this](int, int) { schedule_tabs_write(); });
  connect(tab_widget->bar(), &QTabBar::tabCloseRequested, this, [this](int index) { close_tab(index); });
  tab_widget->bar()->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(tab_widget->bar(), &QWidget::customContextMenuRequested, this,
          [this, bar = tab_widget->bar()](const QPoint& at) { tab_context_menu(bar, at); });
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
  paste_html_ = edit->addAction(tr("Paste from &HTML"), this, [this] { paste_from_html(); });
  paste_html_->setShortcut(QKeySequence(Qt::ControlModifier | Qt::AltModifier | Qt::Key_V));
  const auto read_clipboard = [this] {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    clipboard_html_ = mime != nullptr && mime->hasHtml();
  };
  read_clipboard();
  connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, [this, read_clipboard] {
    read_clipboard();
    if (tabs_ != nullptr && tabs_->count() > 0) update_actions();
  });
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
  show_preview_ = view->addAction(tr("Show &Preview"));
  show_preview_->setCheckable(true);
  connect(show_preview_, &QAction::toggled, this, [this](bool on) {
    set_preview_shown(on);
    settings_.setValue(kPreview, on);
  });
  show_preview_->setChecked(settings_.value(kPreview, false).toBool());
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
  paper_look_action_ = add_view_setting(tr("&Paper look"), paper_look_shown(), [this](bool on) {
    paper_look_ = on;
    settings_.setValue(kPaperLook, on);
    apply_paper_look();
    apply_preview_look();
  });
  paper_look_action_->setEnabled(paper_look_fits(QApplication::palette()));
  QMenu* theme_menu = view->addMenu(tr("T&heme"));
  auto* theme_group = new QActionGroup(this);
  for (const Theme& t : themes()) {
    const QString id = QString::fromLatin1(t.id.data(), static_cast<qsizetype>(t.id.size()));
    QAction* a = theme_menu->addAction(QString::fromUtf8(t.name.data(), static_cast<qsizetype>(t.name.size())));
    a->setCheckable(true);
    a->setChecked(id == theme_);
    theme_group->addAction(a);
    connect(a, &QAction::triggered, this, [this, id] {
      settings_.setValue(kTheme, id);
      set_theme(id);
    });
  }
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

  QMenu* tools = menuBar()->addMenu(tr("&Tools"));
  QAction* mcp_enabled_action = tools->addAction(tr("Enable &MCP Server"));
  mcp_enabled_action->setObjectName(QStringLiteral("mcp_enabled"));
  mcp_enabled_action->setCheckable(true);
  mcp_enabled_action->setChecked(mcp_enabled_);
  connect(mcp_enabled_action, &QAction::toggled, this, [this](bool on) {
    mcp_enabled_ = on;
    settings_.setValue(kMcpEnabled, on);
    update_mcp_server();
  });
  connect(mcp_enabled_action, &QAction::triggered, this, [this](bool on) {
    if (on && mcp_allowed_folders_.isEmpty()) QTimer::singleShot(0, this, [this] { choose_mcp_allowed_folders(); });
  });
  QMenu* socket_menu = tools->addMenu(tr("MCP &Socket Folder"));
  auto* socket_group = new QActionGroup(socket_menu);
  mcp_default_dir_action_ = socket_menu->addAction(tr("&Default"));
  mcp_default_dir_action_->setObjectName(QStringLiteral("mcp_socket_default"));
  mcp_picked_dir_action_ = socket_menu->addAction(QString());
  mcp_picked_dir_action_->setObjectName(QStringLiteral("mcp_socket_picked"));
  for (QAction* action : {mcp_default_dir_action_, mcp_picked_dir_action_}) {
    action->setCheckable(true);
    socket_group->addAction(action);
  }
  connect(mcp_default_dir_action_, &QAction::triggered, this, [this] { set_mcp_use_socket_dir(false); });
  connect(mcp_picked_dir_action_, &QAction::triggered, this, [this] { set_mcp_use_socket_dir(true); });
  socket_menu->addSeparator();
  socket_menu->addAction(tr("&Choose Folder..."), this, [this] { choose_mcp_socket_dir(); })
      ->setObjectName(QStringLiteral("mcp_socket_choose"));
  refresh_mcp_socket_actions();
  tools->addAction(tr("MCP &Allowed Folders..."), this, [this] { choose_mcp_allowed_folders(); })
      ->setObjectName(QStringLiteral("mcp_allowed_folders"));

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
  help->addAction(tr("&Features"), this, [this] { open_help(QStringLiteral("features.md")); });
  help->addAction(tr("&MCP Server"), this, [this] { open_help(QStringLiteral("mcp.md")); });
  help->addSeparator();
  help->addAction(tr("&About lumos-md"), this, [this] { open_help(QStringLiteral("about.md")); });

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

  notice_label_ = new QLabel(this);
  notice_label_->setObjectName(QStringLiteral("notice"));
  notice_label_->setAutoFillBackground(true);
  notice_label_->setContentsMargins(6, 0, 6, 0);
  notice_label_->setVisible(false);
  statusBar()->addWidget(notice_label_);
  notice_timer_ = new QTimer(this);
  notice_timer_->setSingleShot(true);
  notice_timer_->setInterval(10000);
  connect(notice_timer_, &QTimer::timeout, this, [this] { notice_label_->setVisible(false); });

  block_state_ = new QLabel(this);
  statusBar()->addWidget(status_section(ToolIcon::paragraph, block_state_, tr("Block at the cursor"), this));
  path_label_ = new ElidedLabel(this);
  path_label_->setObjectName(QStringLiteral("path"));
  statusBar()->addWidget(status_section(ToolIcon::document, path_label_, QString(), this), 1);
  mcp_status_label_ = new QLabel(tr("MCP"), this);
  mcp_status_label_->setObjectName(QStringLiteral("mcp_status"));
  mcp_status_frame_ = status_section(ToolIcon::mcp, mcp_status_label_, QString(), this, QColor(0xff, 0xd5, 0x4f));
  statusBar()->addPermanentWidget(mcp_status_frame_);
  mcp_status_frame_->setVisible(false);
  position_label_ = new QLabel(this);
  statusBar()->addPermanentWidget(status_section(ToolIcon::caret, position_label_, tr("Line and column"), this));
  words_label_ = new QLabel(this);
  statusBar()->addPermanentWidget(status_section(ToolIcon::words, words_label_, tr("Words"), this));
  words_timer_ = new QTimer(this);
  words_timer_->setSingleShot(true);
  words_timer_->setInterval(300);
  connect(words_timer_, &QTimer::timeout, this, [this] { update_words(); });
  zoom_label_ = new QLabel(this);
  statusBar()->addPermanentWidget(status_section(ToolIcon::zoom, zoom_label_, tr("Zoom"), this));
  set_zoom(zoom_);
  apply_preview_look();

  preview_timer_ = new QTimer(this);
  preview_timer_->setSingleShot(true);
  preview_timer_->setInterval(kPreviewDelayMs);
  connect(preview_timer_, &QTimer::timeout, this, [this] { render_preview(); });

  update_actions();
  update_position();
  update_words();
  update_title();
}

MainWindow::~MainWindow() = default;

bool MainWindow::open_file(const QString& path) { return open_path(path, false); }

bool MainWindow::open_path(const QString& path, bool quiet) {
  bool loaded = false;
  QString error;
  DocumentTab* tab = load_path_tab(path, &loaded, &error);
  if (tab == nullptr) {
    if (!quiet) QMessageBox::warning(this, tr("Open"), tr("Cannot open %1:\n%2").arg(path, error));
    else lumos::log(lumos::LogLevel::debug, kComp, "open_quiet_failed", {{"size", std::to_string(path.size())}});
    return false;
  }
  if (loaded) offer_path_draft(tab);
  return true;
}

std::optional<quint64> MainWindow::open_buffer(const QString& path, QString* error) {
  bool loaded = false;
  DocumentTab* tab = load_path_tab(path, &loaded, error);
  if (tab == nullptr) return std::nullopt;
  if (loaded) {
    QTimer::singleShot(0, this, [this, guard = QPointer<DocumentTab>(tab)] {
      if (guard != nullptr) offer_path_draft(guard);
    });
  }
  return tab->buffer_id();
}

DocumentTab* MainWindow::load_path_tab(const QString& path, bool* loaded, QString* error, bool add_recent) {
  *loaded = false;
  if (const std::optional<quint64> open = find_buffer(path)) {
    DocumentTab* tab = tab_by_id(*open);
    tabs_->setCurrentWidget(tab);
    return tab;
  }
  DocumentTab* current = current_tab();
  const bool reuse = current != nullptr && current->is_blank();
  DocumentTab* tab = reuse ? current : make_tab();
  if (!tab->load(path, false, error)) {
    if (!reuse) delete tab;
    return nullptr;
  }
  *loaded = true;
  if (QFileInfo(path).canonicalPath() == QFileInfo(help_dir()).canonicalFilePath()) tab->editor()->set_read_only(true);
  if (!reuse) tabs_->insertTab(tabs_->currentIndex() + 1, tab, QString());
  update_tab_text(tab);
  settings_.sync();
  if (add_recent) set_recent_files(add_recent_file(settings_.value(kRecentFiles).toStringList(), path));
  const int index = tabs_->indexOf(tab);
  if (tabs_->currentIndex() == index) refresh_for_current_tab();
  else tabs_->setCurrentIndex(index);
  schedule_tabs_write();
  sync_watch();
  return tab;
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
    show_notice(NoticeKind::warning, tr("⚠ A draft was damaged; its text may be incomplete."));
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
        if (find_buffer(draft_read.draft.path)) {
          lumos::log(lumos::LogLevel::debug, kComp, "draft_skip", {{"id", id.toStdString()}, {"reason", "open"}});
          continue;
        }
        open_path(draft_read.draft.path, true);
        continue;
      }
      open_untitled_draft(id);
      if (has_path)
        show_notice(NoticeKind::info, tr("ℹ The file of a draft is gone; its text opened as an Untitled tab."));
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
  sync_watch();
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
  settings_.setValue(kPreviewShare, splitter_->preview_share());
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
  tab->set_buffer_id(next_buffer_id_++);
  using lumos::qt::SourceEditor;
  SourceEditor* editor = tab->editor();
  editor->set_line_numbers(line_numbers_);
  editor->set_max_text_width(limit_width_ ? 100 : 0);
  editor->setFont(current_font());
  if (paper_look_shown()) editor->set_style_table(paper_styles(editor->font(), editor->palette()));
  editor->viewport()->installEventFilter(this);
  editor->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(editor, &QWidget::customContextMenuRequested, this, [this, tab](const QPoint& at) { context_menu(tab, at); });
  tab->set_link_open_failed(
      [this](const QString& url) { show_notice(NoticeKind::warning, tr("⚠ Cannot open %1").arg(url)); });
  tab->set_drafts_dir(drafts_dir_);
  tab->set_draft_changed([this] { schedule_tabs_write(); });
  tab->set_draft_failed([this] { show_notice(NoticeKind::warning, tr("⚠ Cannot write the draft file.")); });
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
      preview_timer_->start();
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
  connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this, tab](int) {
    if (tab == current_tab() && !preview_->isHidden())
      preview_->scroll_to_offset(tab->editor()->offset_at(QPoint(0, 0)));
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
  for (const BufferInfo& buffer : list_buffers()) {
    QAction* a = window_menu_->addAction(QString(buffer.name).replace(QLatin1Char('&'), QStringLiteral("&&")));
    a->setCheckable(true);
    a->setChecked(buffer.current);
    const quint64 id = buffer.id;
    connect(a, &QAction::triggered, this, [this, id] {
      if (DocumentTab* tab = tab_by_id(id)) tabs_->setCurrentWidget(tab);
    });
    window_tab_actions_ << a;
  }
}

QList<BufferInfo> MainWindow::list_buffers() const {
  QList<BufferInfo> list;
  for (int i = 0; i < tabs_->count(); ++i) {
    const DocumentTab* tab = tab_at(i);
    if (tab == nullptr) continue;
    list << BufferInfo{tab->buffer_id(), tab->path(), tab->name(), tab->editor()->is_modified(),
                       i == tabs_->currentIndex(), tab->text_sources()};
  }
  return list;
}

std::optional<QByteArray> MainWindow::read_buffer(quint64 id) const {
  const DocumentTab* tab = tab_by_id(id);
  if (tab == nullptr) return std::nullopt;
  return QByteArray::fromStdString(tab->editor()->text());
}

std::optional<quint64> MainWindow::find_buffer(const QString& path) const {
  if (path.isEmpty()) return std::nullopt;
  const QString key = path_key(path);
  for (int i = 0; i < tabs_->count(); ++i) {
    const DocumentTab* tab = tab_at(i);
    if (tab != nullptr && !tab->path().isEmpty() && path_key(tab->path()) == key)
      return tab->buffer_id();
  }
  return std::nullopt;
}

void MainWindow::set_mcp_server_name(const QString& name) {
  mcp_server_name_ = name;
  update_mcp_server();
}

bool MainWindow::mcp_listening() const { return mcp_server_ && mcp_server_->is_listening(); }

void MainWindow::update_mcp_server() {
  if (!mcp_enabled_ || mcp_server_name_.isEmpty()) {
    mcp_server_.reset();
    refresh_mcp_status();
    return;
  }
  const QString name = mcp_listen_name();
  if (mcp_server_ && mcp_server_->is_listening() && mcp_server_->name() == name) {
    refresh_mcp_status();
    return;
  }
  mcp_server_ = std::make_unique<McpServer>(*this);
  if (!mcp_server_->listen(name)) {
    show_notice(NoticeKind::warning, tr("⚠ The MCP server cannot listen on %1.").arg(QDir::toNativeSeparators(name)));
  } else {
    const QStringList effective = mcp_effective_folders();
    for (const QString& folder : mcp_allowed_folders_) {
      if (effective.contains(folder)) continue;
      show_notice(NoticeKind::warning, tr("⚠ MCP ignores %1: it holds the lumos-md settings or drafts.")
                                            .arg(QDir::toNativeSeparators(folder)));
      lumos::log(lumos::LogLevel::debug, kComp, "mcp_folder_ignored", {});
    }
  }
  refresh_mcp_status();
}

QString MainWindow::mcp_listen_name() const {
  return mcp_use_socket_dir_ ? mcp_socket_in(mcp_socket_dir_) : mcp_server_name_;
}

void MainWindow::set_mcp_use_socket_dir(bool on) {
  mcp_use_socket_dir_ = on && !mcp_socket_dir_.isEmpty();
  settings_.setValue(kMcpUseSocketDir, mcp_use_socket_dir_);
  refresh_mcp_socket_actions();
  update_mcp_server();
}

void MainWindow::choose_mcp_socket_dir() {
  const QString start = mcp_socket_dir_.isEmpty() ? QDir::homePath() : mcp_socket_dir_;
  const QString dir = QFileDialog::getExistingDirectory(this, tr("MCP Socket Folder"), start);
  if (dir.isEmpty()) return;
  mcp_socket_dir_ = QDir(dir).absolutePath();
  settings_.setValue(kMcpSocketDir, mcp_socket_dir_);
  set_mcp_use_socket_dir(true);

  QString arg = QDir::toNativeSeparators(mcp_socket_dir_);
  static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9/._+-]+$"));
  if (!plain.match(arg).hasMatch()) arg = "'" + arg.replace("'", "'\\''") + "'";
  QMessageBox box(QMessageBox::Information, tr("MCP Socket Folder"),
                  tr("lumos-md listens on %1 while Tools -> Enable MCP Server is on. In the agent's settings, "
                     "start the MCP server with this command:")
                      .arg(QDir::toNativeSeparators(mcp_socket_in(mcp_socket_dir_))),
                  QMessageBox::Ok, this);
  box.setInformativeText(QStringLiteral("lumos-md --mcp --mcp-dir ") + arg);
  box.setTextInteractionFlags(Qt::TextSelectableByMouse);
  box.exec();
}

void MainWindow::refresh_mcp_socket_actions() {
  if (mcp_default_dir_action_ == nullptr) return;
  mcp_picked_dir_action_->setText(QDir::toNativeSeparators(mcp_socket_dir_).replace('&', QStringLiteral("&&")));
  mcp_picked_dir_action_->setVisible(!mcp_socket_dir_.isEmpty());
  (mcp_use_socket_dir_ ? mcp_picked_dir_action_ : mcp_default_dir_action_)->setChecked(true);
}

QStringList MainWindow::mcp_protected_dirs() const {
  QStringList dirs;
  if (!settings_.fileName().isEmpty()) dirs << mcp_path_key(QFileInfo(settings_.fileName()).absolutePath());
  if (!open_tabs_file_.isEmpty()) dirs << mcp_path_key(QFileInfo(open_tabs_file_).absolutePath());
  if (!drafts_dir_.isEmpty()) dirs << mcp_path_key(drafts_dir_);
  return dirs;
}

bool MainWindow::mcp_folder_holds_protected(const QString& folder) const {
  for (const QString& protected_dir : mcp_protected_dirs())
    if (mcp_path_allowed(protected_dir, {folder})) return true;
  return false;
}

QStringList MainWindow::mcp_allowed_folders() const { return mcp_allowed_folders_; }

QStringList MainWindow::set_mcp_allowed_folders(const QStringList& folders) {
  QStringList stored;
  QStringList refused;
  QStringList seen;
  for (const QString& folder : folders) {
    const QString canonical = QFileInfo(folder).canonicalFilePath();
    if (canonical.isEmpty() || seen.contains(canonical)) continue;  
    seen << canonical;
    if (mcp_folder_holds_protected(canonical)) {
      refused << canonical;
      continue;
    }
    stored << canonical;
  }
  mcp_allowed_folders_ = stored;
  settings_.setValue(kMcpAllowedFolders, mcp_allowed_folders_);
  refresh_mcp_status();
  return refused;
}

QStringList MainWindow::mcp_effective_folders() const {
  QStringList effective;
  for (const QString& folder : mcp_allowed_folders_)
    if (!mcp_folder_holds_protected(folder)) effective << folder;
  return effective;
}

void MainWindow::choose_mcp_allowed_folders() {
  QDialog dialog(this);
  dialog.setWindowTitle(tr("MCP Allowed Folders"));
  auto* layout = new QVBoxLayout(&dialog);
  layout->addWidget(new QLabel(
      tr("Agents get only the open files in these folders. Without a folder they get no file."), &dialog));
  auto* list = new QListWidget(&dialog);
  list->setObjectName(QStringLiteral("folders"));
  list->setSelectionMode(QAbstractItemView::ExtendedSelection);
  for (const QString& folder : mcp_allowed_folders_) list->addItem(QDir::toNativeSeparators(folder));
  layout->addWidget(list);
  auto* buttons_row = new QHBoxLayout();
  auto* add_button = new QPushButton(tr("&Add..."), &dialog);
  auto* remove_button = new QPushButton(tr("&Remove"), &dialog);
  buttons_row->addWidget(add_button);
  buttons_row->addWidget(remove_button);
  buttons_row->addStretch(1);
  layout->addLayout(buttons_row);
  connect(add_button, &QPushButton::clicked, &dialog, [this, &dialog, list] {
    const QString dir = QFileDialog::getExistingDirectory(&dialog, tr("Add Folder"), QDir::homePath());
    if (dir.isEmpty()) return;
    const QString canonical = QFileInfo(dir).canonicalFilePath();
    if (canonical.isEmpty()) return;
    if (mcp_folder_holds_protected(canonical)) {
      QMessageBox::warning(&dialog, tr("MCP Allowed Folders"),
                            tr("%1 holds the lumos-md settings or drafts, so an agent could change this list. "
                               "Pick a folder inside it.")
                                .arg(QDir::toNativeSeparators(canonical)));
      return;
    }
    const QString native = QDir::toNativeSeparators(canonical);
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->text() == native) return;  
    list->addItem(native);
  });
  connect(remove_button, &QPushButton::clicked, list, [list] {
    for (QListWidgetItem* item : list->selectedItems()) delete item;
  });
  auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(box);
  if (dialog.exec() != QDialog::Accepted) return;
  QStringList folders;
  for (int i = 0; i < list->count(); ++i) folders << QDir::fromNativeSeparators(list->item(i)->text());
  set_mcp_allowed_folders(folders);
}

void MainWindow::refresh_mcp_status() {
  if (mcp_status_frame_ == nullptr) return;
  const bool on = mcp_listening();
  mcp_status_frame_->setVisible(on);
  if (!on) return;
  const QStringList folders = mcp_effective_folders();
  if (folders.isEmpty()) {
    mcp_status_frame_->setToolTip(
        tr("MCP server on. No allowed folder: agents get no file. Tools -> MCP Allowed Folders."));
    return;
  }
  QStringList native;
  for (const QString& folder : folders) native << QDir::toNativeSeparators(folder);
  mcp_status_frame_->setToolTip(tr("MCP server on. Agents get the open files in:\n%1").arg(native.join('\n')));
}

DocumentTab* MainWindow::tab_by_id(quint64 id) const {
  for (int i = 0; i < tabs_->count(); ++i) {
    DocumentTab* tab = tab_at(i);
    if (tab != nullptr && tab->buffer_id() == id) return tab;
  }
  return nullptr;
}

bool MainWindow::paper_look_shown() const { return paper_look_ && paper_look_fits(QApplication::palette()); }

void MainWindow::apply_paper_look() {
  const bool on = paper_look_shown();
  for_each_tab([on](DocumentTab* tab) {
    lumos::qt::SourceEditor* editor = tab->editor();
    if (on) editor->set_style_table(paper_styles(editor->font(), editor->palette()));
    else editor->use_default_style_table();
  });
}

void MainWindow::set_theme(const QString& id) {
  const Theme& theme = find_theme(id);
  theme_ = QString::fromLatin1(theme.id.data(), static_cast<qsizetype>(theme.id.size()));
  apply_theme(theme);
  {
    const QSignalBlocker block(paper_look_action_);
    paper_look_action_->setChecked(paper_look_shown());
  }
  paper_look_action_->setEnabled(paper_look_fits(QApplication::palette()));
  if (paper_look_) apply_paper_look();
  apply_preview_look();
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

void MainWindow::apply_preview_look() {
  QFont f = QApplication::font();
  f.setPointSizeF(font_size_ * zoom_);
  preview_->setFont(f);
  const QPalette pal = palette();
  const QColor base = pal.color(QPalette::Active, QPalette::Base);
  const QColor text = pal.color(QPalette::Active, QPalette::Text);
  const QColor page = mix(base, QColor(0xc8, 0xa0, 0x58), paper_look_shown() ? 0.1 : 0.05);
  preview_->set_look(page, mix(page, text, 0.35), mix(page, text, 0.07));
  preview_box_->layout()->setContentsMargins(0, tabs_->tabBar()->sizeHint().height(), 0, 0);
  render_preview();
}

void MainWindow::set_preview_shown(bool on) {
  if (on) {
    preview_box_->show();
    preview_->show();
    splitter_->restore_split();
  } else {
    preview_box_->hide();  
    preview_->hide();
    preview_stale_ = true;
  }
}

void MainWindow::render_preview() {
  DocumentTab* const tab = current_tab();
  if (tab == nullptr || preview_->isHidden() || preview_->width() <= 0) {
    preview_stale_ = true;
    return;
  }
  preview_->show_document(tab->editor()->document(), tab->path());
  preview_->scroll_to_offset(tab->editor()->offset_at(QPoint(0, 0)));
  preview_stale_ = false;
}

void MainWindow::on_preview_size_changed() {
  const bool shown = !preview_->isHidden() && preview_->width() > 0;
  if (shown && preview_stale_) render_preview();
  else if (!shown) preview_stale_ = true;
}

void MainWindow::apply_font() {
  const QFont f = current_font();
  for_each_tab([&f](DocumentTab* tab) { tab->editor()->setFont(f); });
  if (paper_look_shown()) apply_paper_look();
  apply_preview_look();
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
                                                          tr("Markdown (%1);;All files (*)").arg(text_file_patterns()));
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
    const QString& path = list[i];
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

void MainWindow::show_notice(NoticeKind kind, const QString& text) {
  notice_label_->setText(text);
  QPalette pal = notice_label_->palette();
  if (kind == NoticeKind::info) {
    pal.setColor(QPalette::Window, pal.color(QPalette::Highlight));
    pal.setColor(QPalette::WindowText, pal.color(QPalette::HighlightedText));
  } else {
    pal.setColor(QPalette::Window, QColor(0xff, 0xd5, 0x4f));
    pal.setColor(QPalette::WindowText, Qt::black);
  }
  notice_label_->setPalette(pal);
  notice_label_->setVisible(true);
  notice_timer_->start();
}

void MainWindow::sync_watch() {
  QStringList files;
  QStringList dirs;
  for (int i = 0; i < tabs_->count(); ++i) {
    DocumentTab* tab = tab_at(i);
    if (tab == nullptr || tab->path().isEmpty()) continue;
    if (QFileInfo::exists(tab->path())) files << tab->path();
    const QString dir = QFileInfo(tab->path()).absolutePath();
    if (!dirs.contains(dir)) dirs << dir;
  }
  const QStringList old_files = file_watcher_->files();
  const QStringList old_dirs = file_watcher_->directories();
  QStringList remove_files;
  QStringList add_files;
  for (const QString& f : old_files) {
    if (!files.contains(f)) remove_files << f;
  }
  for (const QString& f : files) {
    if (!old_files.contains(f)) add_files << f;
  }
  QStringList remove_dirs;
  QStringList add_dirs;
  for (const QString& d : old_dirs) {
    if (!dirs.contains(d)) remove_dirs << d;
  }
  for (const QString& d : dirs) {
    if (!old_dirs.contains(d)) add_dirs << d;
  }
  if (!remove_files.isEmpty()) file_watcher_->removePaths(remove_files);
  if (!add_files.isEmpty()) file_watcher_->addPaths(add_files);
  if (!remove_dirs.isEmpty()) file_watcher_->removePaths(remove_dirs);
  if (!add_dirs.isEmpty()) file_watcher_->addPaths(add_dirs);
}

MainWindow::ChangedAnswer MainWindow::ask_changed(DocumentTab* tab, bool from_save) {
  disk_dialog_open_ = true;
  const auto read_file = [&](QByteArray* out) {
    QFile f(tab->path());
    if (!f.open(QIODevice::ReadOnly)) return false;
    *out = f.readAll();
    return true;
  };
  for (;;) {
    QByteArray seen;
    if (!read_file(&seen)) {
      disk_dialog_open_ = false;
      return ChangedAnswer::cancel;
    }
    const QByteArray seen_sha = sha256_hex(seen);
    const std::string yours = tab->editor()->text();
    const LineMerge merge =
        merge_lines(std::string_view(tab->base().constData(), static_cast<std::size_t>(tab->base().size())), yours,
                    std::string_view(seen.constData(), static_cast<std::size_t>(seen.size())));
    const QString title = from_save ? tr("Save") : tr("File changed");
    const QString text = from_save
                              ? tr("%1 was changed on disk since it was opened or saved.").arg(tab->name())
                              : tr("%1 was changed on disk.").arg(tab->name());
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::NoButton, this);
    QString info = tr("Show diff shows both texts and merges them. Save as shown writes your text over the file. "
                       "Load changes loads the file; Undo then gives your text back.");
    if (merge.too_many) {
      info += QLatin1Char('\n') + tr("The changes are too many to show.");
      lumos::log(lumos::LogLevel::debug, kComp, "disk_merge_too_many");
    }
    box.setInformativeText(info);
    QPushButton* diff_button = box.addButton(tr("Show diff"), QMessageBox::AcceptRole);
    diff_button->setEnabled(!merge.too_many);
    QPushButton* save_button = box.addButton(tr("Save as shown"), QMessageBox::AcceptRole);
    save_button->setEnabled(!tab->read_only_file());
    QPushButton* load_button = box.addButton(tr("Load changes"), QMessageBox::AcceptRole);
    QPushButton* cancel_button = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton((!from_save && !merge.too_many) ? diff_button : cancel_button);
    box.setEscapeButton(cancel_button);
    box.exec();
    QAbstractButton* clicked = box.clickedButton();
    if (clicked == diff_button) {
      MergeWindow window(merge, tab->name(), MergeNames{tr("On disk")}, this);
      lumos::log(
          lumos::LogLevel::debug, kComp, "disk_merge_open",
          {{"chunks", std::to_string(merge.chunks.size())}, {"conflicts", std::to_string(merge.conflicts())}});
      if (window.exec() != QDialog::Accepted) continue;  
      const std::string result = window.result();
      const QByteArray result_bytes(result.data(), static_cast<qsizetype>(result.size()));
      const bool modified = result_bytes != seen;
      tab->use_merge(result_bytes, seen);
      lumos::log(lumos::LogLevel::debug, kComp, "disk_merge_use",
                 {{"bytes", std::to_string(result.size())}, {"modified", modified ? "1" : "0"}});
      update_tab_text(tab);
      update_title();
      update_actions();
      update_words();
      show_notice(NoticeKind::info, tr("ℹ %1: merged with the file on disk. Save writes the result.").arg(tab->name()));
      disk_dialog_open_ = false;
      return ChangedAnswer::merged;
    }
    if (clicked == save_button) {
      QByteArray now;
      if (!read_file(&now)) {
        disk_dialog_open_ = false;
        return ChangedAnswer::cancel;
      }
      if (sha256_hex(now) != seen_sha) continue;  
      const bool written = write_tab(tab, tab->path());
      lumos::log(lumos::LogLevel::debug, kComp, "disk_save_shown", {{"ok", written ? "1" : "0"}});
      disk_dialog_open_ = false;
      return written ? ChangedAnswer::saved : ChangedAnswer::cancel;
    }
    if (clicked == load_button) {
      tab->reload_from_disk(seen);
      update_tab_text(tab);
      update_title();
      update_actions();
      update_words();
      show_notice(NoticeKind::info,
                  tr("↻ %1 was reloaded: it changed on disk. Undo gives your text back.").arg(tab->name()));
      disk_dialog_open_ = false;
      return ChangedAnswer::loaded;
    }
    if (!from_save) {
      tab->set_kept_sha256(seen_sha);
      lumos::log(lumos::LogLevel::debug, kComp, "disk_keep");
    }
    disk_dialog_open_ = false;
    return ChangedAnswer::cancel;
  }
}

void MainWindow::handle_current() {
  if (!isActiveWindow() || disk_dialog_open_ || QApplication::activeModalWidget() != nullptr) return;
  DocumentTab* tab = current_tab();
  if (tab == nullptr) return;
  auto reload = [this, tab] {
    QFile f(tab->path());
    if (!f.open(QIODevice::ReadOnly)) return;  
    tab->reload_from_disk(f.readAll());
    update_tab_text(tab);
    update_title();
    update_actions();
    update_words();
    show_notice(NoticeKind::info,
                tr("↻ %1 was reloaded: it changed on disk. Undo gives your text back.").arg(tab->name()));
    sync_watch();
  };
  if (tab->disk_pending() == DiskState::changed && !tab->editor()->is_modified()) {
    reload();
    return;
  }
  if (tab->disk_pending() == DiskState::changed) {
    lumos::log(lumos::LogLevel::debug, kComp, "disk_changed", {{"modified", "1"}});
    ask_changed(tab, false);
    tab->set_disk_pending(DiskState::same);
    watch_timer_->start();
    return;
  }
  if (tab->disk_pending() == DiskState::gone) {
    lumos::log(lumos::LogLevel::debug, kComp, "disk_gone");
    QMessageBox box(QMessageBox::Warning, tr("File gone"), tr("%1 was deleted or moved.").arg(tab->name()),
                    QMessageBox::NoButton, this);
    box.setInformativeText(tr("Keep in editor keeps the text; Save writes the file again."));
    QPushButton* keep_button = box.addButton(tr("Keep in editor"), QMessageBox::AcceptRole);
    box.addButton(tr("Close"), QMessageBox::DestructiveRole);
    box.setDefaultButton(keep_button);
    box.setEscapeButton(keep_button);
    disk_dialog_open_ = true;
    box.exec();
    disk_dialog_open_ = false;
    if (box.clickedButton() == keep_button) {
      tab->editor()->mark_unsaved();
      tab->write_draft_now();
      tab->set_gone_kept(true);
      tab->set_disk_pending(DiskState::same);
      update_tab_text(tab);
      update_title();
      lumos::log(lumos::LogLevel::debug, kComp, "disk_keep", {{"gone", "1"}});
    } else {
      close_tab(tabs_->indexOf(tab));
    }
    watch_timer_->start();
    return;
  }
}

void MainWindow::check_files() {
  for (int i = 0; i < tabs_->count(); ++i) {
    DocumentTab* tab = tab_at(i);
    if (tab == nullptr || tab->path().isEmpty()) continue;
    if (tab->update_read_only_file() && tab == current_tab()) update_title();
    const DiskCheck result = check_disk(tab->path(), tab->saved_sha256(), tab->kept_sha256());
    if (result.state == DiskState::gone) {
      tab->set_disk_pending(tab->gone_kept() ? DiskState::same : DiskState::gone);
    } else {
      if (tab->gone_kept()) tab->set_gone_kept(false);
      tab->set_disk_pending(result.state);
    }
  }
  handle_current();
  sync_watch();
}

void MainWindow::changeEvent(QEvent* e) {
  QMainWindow::changeEvent(e);
  if (e->type() == QEvent::ActivationChange && isActiveWindow()) watch_timer_->start();
}

void MainWindow::open_help(const QString& name) {
  const QString path = help_dir() + QLatin1Char('/') + name;
  bool loaded = false;
  QString error;
  DocumentTab* tab = load_path_tab(path, &loaded, &error, false);
  if (tab == nullptr) {
    lumos::log(lumos::LogLevel::debug, kComp, "help_failed", {{"size", std::to_string(path.size())}});
    QMessageBox::warning(this, tr("Help"), tr("Cannot open the help file %1:\n%2").arg(path, error));
  }
}

void MainWindow::table_move(const std::optional<lumos::CommandResult>& result) {
  current_tab()->editor()->apply_command(result);
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
  paste_html_->setEnabled(!ro && clipboard_html_);

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

void MainWindow::tab_context_menu(QTabBar* bar, const QPoint& at) {
  const DocumentTab* tab = tab_at(bar->tabAt(at));
  if (tab == nullptr) return;
  const QString path = QDir::toNativeSeparators(tab->path());
  QMenu menu(this);
  QAction* copy_path =
      menu.addAction(tr("Copy &Path"), this, [path] { QGuiApplication::clipboard()->setText(path); });
  copy_path->setObjectName(QStringLiteral("tab_copy_path"));
  copy_path->setEnabled(!path.isEmpty());
  menu.exec(bar->mapToGlobal(at));
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
  menu.addAction(paste_html_);
  menu.addSeparator();
  menu.addAction(show_preview_);
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

void MainWindow::paste_from_html() {
  lumos::qt::SourceEditor* editor = current_tab()->editor();
  const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
  if (editor->is_read_only() || mime == nullptr || !mime->hasHtml()) return;
  std::optional<lumos::CommandResult> result =
      paste_html(editor->document(), editor->selection(), mime->html(), editor->newline());
  if (result)
    editor->apply_command(result);
  else
    editor->paste();
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
  if (tab->update_read_only_file() && tab == current_tab()) update_title();
  if (tab->read_only_file()) return save_as(tab);
  const DiskCheck result = check_disk(tab->path(), tab->saved_sha256(), tab->kept_sha256());
  if (result.state == DiskState::changed) return ask_changed(tab, true) == ChangedAnswer::saved;
  return write_tab(tab, tab->path());
}

bool MainWindow::save_as(DocumentTab* tab) {
  const QString path = QFileDialog::getSaveFileName(this, tr("Save As"), tab->path());
  if (path.isEmpty()) return false;
  const std::optional<quint64> open = find_buffer(path);
  if (open && *open != tab->buffer_id()) {
    lumos::log(lumos::LogLevel::debug, kComp, "save_as_refused", {{"reason", "open_in_other_tab"}});
    QMessageBox::warning(this, tr("Save As"),
                         tr("%1 is open in another tab. Close that tab first, or choose another name.")
                             .arg(QFileInfo(path).fileName()));
    return false;
  }
  if (!write_tab(tab, path)) return false;
  tab->set_path(path);
  schedule_tabs_write();
  sync_watch();
  settings_.sync();
  set_recent_files(add_recent_file(settings_.value(kRecentFiles).toStringList(), path));
  update_tab_text(tab);
  if (tab == current_tab()) {
    update_title();
    update_actions();
    render_preview();
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
  setWindowTitle((tab->read_only_file() ? tr("%1[*] [Read-Only] - lumos-md") : tr("%1[*] - lumos-md"))
                     .arg(tab->name()));
  setWindowModified(tab->editor()->is_modified());
  const QString path = QDir::toNativeSeparators(tab->path());
  path_label_->setText(path);
  path_label_->parentWidget()->setToolTip(path);
}
