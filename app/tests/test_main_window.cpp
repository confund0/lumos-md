
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QMenu>
#include <QMimeData>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <memory>

#include "dialog_util.hpp"
#include "file_modes.hpp"
#include "document_tab.hpp"
#include "main_window.hpp"
#include "open_tabs.hpp"
#include "theme.hpp"
#include "lumos/qt/source_editor.hpp"

namespace {

void set_clipboard(const QString& html, const QString& text) {
  auto* mime = new QMimeData;
  if (!html.isEmpty()) mime->setHtml(html);
  mime->setText(text);
  QGuiApplication::clipboard()->setMimeData(mime);
}

QString write_file(const QDir& dir, const QString& name, const QString& text) {
  const QString path = dir.filePath(name);
  QFile f(path);
  const bool ok = f.open(QIODevice::WriteOnly);
  Q_ASSERT(ok);
  Q_UNUSED(ok);
  f.write(text.toUtf8());
  f.close();
  return path;
}

QString write_bytes(const QDir& dir, const QString& name, const QByteArray& bytes) {
  const QString path = dir.filePath(name);
  QFile f(path);
  const bool ok = f.open(QIODevice::WriteOnly);
  Q_ASSERT(ok);
  Q_UNUSED(ok);
  f.write(bytes);
  f.close();
  return path;
}

QByteArray read_bytes(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  return f.readAll();
}

QByteArray editor_bytes(DocumentTab* tab) { return QByteArray::fromStdString(tab->editor()->text()); }

}  

class TestMainWindow : public QObject {
  Q_OBJECT

 private slots:
  void init();
  void cleanup();

  void start_and_open();
  void open_existing_activates();
  void new_tab_after_current();
  void close_tab_leaves_blank();
  void step_tab_wraps();
  void window_shortcuts_reach_window();
  void typed_char_marks_tab();
  void line_numbers_apply_to_new_tab();
  void close_other_tab_keeps_current();
  void ampersand_in_tab_text();
  void relative_path_stored_absolute();
  void corner_buttons_in_bar_middle();
  void close_tab_with_path_goes_to_closed_tabs();
  void close_blank_tab_leaves_closed_tabs_unchanged();
  void reopen_closed_tab_opens_it_again();
  void reopen_open_path_makes_current_adds_no_tab();
  void reopen_skips_deleted_file();
  void closed_tabs_cap_is_ten();
  void window_close_writes_open_tabs_and_restore_reads_them();
  void restore_tabs_skips_missing_file();
  void restore_tabs_all_missing_leaves_blank_tab();
  void tab_changes_write_open_tabs();
  void paste_html_enabled_only_with_html();
  void paste_html_is_one_undo_step();
  void paste_html_key_reaches_window();
  void paste_html_in_code_block_is_plain_paste();
  void open_and_save_keep_exact_bytes();
  void save_as_untitled_sets_path();
  void save_as_cancel_keeps_untitled();
  void save_as_onto_other_tab_refused();
  void save_as_onto_deleted_file_of_other_tab_refused();
  void open_missing_file_shows_dialog_adds_no_tab();
  void open_same_file_other_spelling_activates();
  void close_modified_tab_save_writes_then_closes();
  void quit_asks_in_tab_order_cancel_stops();
  void revert_loads_saved_text();
  void window_menu_lists_tabs_and_switches();
  void list_buffers_in_tab_order_with_ids();
  void read_buffer_gives_exact_bytes();
  void find_buffer_by_any_spelling();
  void read_only_file_save_opens_save_as();
  void read_only_file_cleared_by_chmod();
  void read_only_dir_makes_file_read_only();
  void theme_menu_applies_and_saves();
  void theme_default_gives_system_palette_back();
  void theme_unknown_id_and_unknown_role();
  void theme_png();
  void tab_close_button_closes_its_tab();

 private:
  QAction* find_action(const QString& text) const;
  QTabWidget* tab_widget() const { return window_->tabs_for_tests(); }

  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<MainWindow> window_;
};

void TestMainWindow::init() {
  dir_ = std::make_unique<QTemporaryDir>();
  QVERIFY(dir_->isValid());
  window_ = std::make_unique<MainWindow>(QDir(dir_->path()).filePath(QStringLiteral("settings.ini")));
}

void TestMainWindow::cleanup() {
  window_.reset();
  dir_.reset();
}

QAction* TestMainWindow::find_action(const QString& text) const {
  for (QAction* a : window_->findChildren<QAction*>())
    if (QString(a->text()).remove(QLatin1Char('&')) == text) return a;
  return nullptr;
}

void TestMainWindow::start_and_open() {
  QCOMPARE(window_->tab_count(), 1);
  QVERIFY(window_->current_tab()->is_blank());

  const QString a = write_file(QDir(dir_->path()), QStringLiteral("a.md"), QStringLiteral("# A\n"));
  QVERIFY(window_->open_file(a));
  QCOMPARE(window_->tab_count(), 1);
  QCOMPARE(window_->current_tab()->path(), a);

  const QString b = write_file(QDir(dir_->path()), QStringLiteral("b.md"), QStringLiteral("# B\n"));
  QVERIFY(window_->open_file(b));
  QCOMPARE(window_->tab_count(), 2);
  QCOMPARE(window_->current_tab()->path(), b);
  QCOMPARE(window_->tab_at(0)->path(), a);
  QCOMPARE(window_->tab_at(1)->path(), b);
}

void TestMainWindow::open_existing_activates() {
  const QDir dir(dir_->path());
  const QString a = write_file(dir, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QString b = write_file(dir, QStringLiteral("b.md"), QStringLiteral("# B\n"));
  QVERIFY(window_->open_file(a));
  QVERIFY(window_->open_file(b));
  QCOMPARE(window_->tab_count(), 2);

  const QString link = dir.filePath(QStringLiteral("a_link.md"));
  QVERIFY(QFile::link(a, link));
  QVERIFY(window_->open_file(link));
  QCOMPARE(window_->tab_count(), 2);
  QCOMPARE(window_->current_tab()->path(), a);
}

void TestMainWindow::new_tab_after_current() {
  const QDir dir(dir_->path());
  const QString a = write_file(dir, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QString b = write_file(dir, QStringLiteral("b.md"), QStringLiteral("# B\n"));
  QVERIFY(window_->open_file(a));
  QVERIFY(window_->open_file(b));
  QVERIFY(window_->open_file(a));  
  QCOMPARE(window_->tab_count(), 2);

  window_->new_tab();
  QCOMPARE(window_->tab_count(), 3);
  DocumentTab* new_one = window_->current_tab();
  QVERIFY(new_one->is_blank());
  QCOMPARE(window_->tab_at(0)->path(), a);
  QCOMPARE(window_->tab_at(1), new_one);
  QCOMPARE(window_->tab_at(2)->path(), b);
}

void TestMainWindow::close_tab_leaves_blank() {
  const QString a = write_file(QDir(dir_->path()), QStringLiteral("a.md"), QStringLiteral("# A\n"));
  QVERIFY(window_->open_file(a));  
  QCOMPARE(window_->tab_count(), 1);

  window_->new_tab();
  QCOMPARE(window_->tab_count(), 2);
  QVERIFY(window_->close_tab(1));
  QCOMPARE(window_->tab_count(), 1);

  QVERIFY(window_->close_tab(0));
  QCOMPARE(window_->tab_count(), 1);
  QVERIFY(window_->current_tab()->is_blank());
}

void TestMainWindow::step_tab_wraps() {
  window_->new_tab();
  window_->new_tab();
  QCOMPARE(window_->tab_count(), 3);
  QTabWidget* tabs = tab_widget();

  tabs->setCurrentIndex(2);
  window_->step_tab(1);
  QCOMPARE(tabs->currentIndex(), 0);

  tabs->setCurrentIndex(0);
  window_->step_tab(-1);
  QCOMPARE(tabs->currentIndex(), 2);
}

void TestMainWindow::window_shortcuts_reach_window() {
  window_->new_tab();
  window_->new_tab();
  QTabWidget* tabs = tab_widget();
  tabs->setCurrentIndex(0);
  window_->show();
  QVERIFY(QTest::qWaitForWindowExposed(window_.get()));
  window_->activateWindow();
  lumos::qt::SourceEditor* editor = window_->current_tab()->editor();
  editor->setFocus();
  QVERIFY(QTest::qWaitForWindowActive(window_.get()));

  QTest::keyClick(editor, Qt::Key_Tab, Qt::ControlModifier);
  QCOMPARE(tabs->currentIndex(), 1);

  editor = window_->current_tab()->editor();
  editor->setFocus();
  QTest::keyClick(editor, Qt::Key_PageUp, Qt::ControlModifier);
  QCOMPARE(tabs->currentIndex(), 0);
}

void TestMainWindow::typed_char_marks_tab() {
  window_->new_tab();
  QCOMPARE(window_->tab_count(), 2);
  DocumentTab* first = window_->tab_at(0);
  DocumentTab* second = window_->tab_at(1);
  QTabWidget* tabs = tab_widget();
  QCOMPARE(tabs->currentIndex(), tabs->indexOf(second));

  window_->show();
  QVERIFY(QTest::qWaitForWindowExposed(window_.get()));
  window_->activateWindow();
  second->editor()->setFocus();
  QVERIFY(QTest::qWaitForWindowActive(window_.get()));
  QTest::keyClicks(second->editor(), QStringLiteral("x"));

  QVERIFY(tabs->tabText(tabs->indexOf(second)).endsWith(QStringLiteral(" *")));
  QVERIFY(!tabs->tabText(tabs->indexOf(first)).endsWith(QStringLiteral(" *")));
}

void TestMainWindow::line_numbers_apply_to_new_tab() {
  QVERIFY(window_->current_tab()->editor()->line_numbers());
  QAction* line_numbers = find_action(QStringLiteral("Line numbers"));
  QVERIFY(line_numbers != nullptr);
  line_numbers->trigger();
  QVERIFY(!window_->current_tab()->editor()->line_numbers());

  window_->new_tab();
  QVERIFY(!window_->current_tab()->editor()->line_numbers());
}

void TestMainWindow::close_other_tab_keeps_current() {
  window_->new_tab();
  window_->new_tab();
  DocumentTab* current = window_->tab_at(2);
  tab_widget()->setCurrentIndex(2);
  QVERIFY(window_->close_tab(0));
  QCOMPARE(window_->current_tab(), current);
}

void TestMainWindow::ampersand_in_tab_text() {
  const QString a = write_file(QDir(dir_->path()), QStringLiteral("a&b.md"), QStringLiteral("x\n"));
  QVERIFY(window_->open_file(a));
  QCOMPARE(tab_widget()->tabText(0), QStringLiteral("a&&b.md"));
}

void TestMainWindow::close_tab_with_path_goes_to_closed_tabs() {
  const QString a = write_file(QDir(dir_->path()), QStringLiteral("ca.md"), QStringLiteral("x\n"));
  QVERIFY(window_->open_file(a));
  QVERIFY(window_->close_tab(0));
  QCOMPARE(window_->closed_tabs(), QStringList{a});
}

void TestMainWindow::close_blank_tab_leaves_closed_tabs_unchanged() {
  window_->new_tab();
  QVERIFY(window_->close_tab(1));
  QVERIFY(window_->closed_tabs().isEmpty());
}

void TestMainWindow::reopen_closed_tab_opens_it_again() {
  const QString a = write_file(QDir(dir_->path()), QStringLiteral("rb.md"), QStringLiteral("x\n"));
  QVERIFY(window_->open_file(a));
  QVERIFY(window_->close_tab(0));
  QVERIFY(window_->current_tab()->is_blank());

  window_->reopen_closed_tab();
  QCOMPARE(window_->tab_count(), 1);
  QCOMPARE(window_->current_tab()->path(), a);
  QVERIFY(window_->closed_tabs().isEmpty());
}

void TestMainWindow::reopen_open_path_makes_current_adds_no_tab() {
  const QString a = write_file(QDir(dir_->path()), QStringLiteral("rc.md"), QStringLiteral("x\n"));
  QVERIFY(window_->open_file(a));
  window_->new_tab();
  QTabWidget* tabs = tab_widget();
  tabs->setCurrentIndex(tabs->indexOf(window_->tab_at(0)));
  QVERIFY(window_->close_tab(0));  
  QCOMPARE(window_->tab_count(), 1);

  QVERIFY(window_->open_file(a));  
  QCOMPARE(window_->tab_count(), 1);
  QCOMPARE(window_->closed_tabs(), QStringList{a});

  window_->reopen_closed_tab();
  QCOMPARE(window_->tab_count(), 1);  
  QCOMPARE(window_->current_tab()->path(), a);
  QVERIFY(window_->closed_tabs().isEmpty());
}

void TestMainWindow::reopen_skips_deleted_file() {
  const QDir dir(dir_->path());
  const QString a = write_file(dir, QStringLiteral("rd_a.md"), QStringLiteral("x\n"));
  QVERIFY(window_->open_file(a));
  window_->new_tab();
  QVERIFY(window_->close_tab(0));  

  const QString b = write_file(dir, QStringLiteral("rd_b.md"), QStringLiteral("x\n"));
  QVERIFY(window_->open_file(b));  
  QVERIFY(window_->close_tab(0));  
  QVERIFY(QFile::remove(b));

  window_->reopen_closed_tab();
  QVERIFY(window_->closed_tabs().isEmpty());
  QCOMPARE(window_->tab_count(), 1);
  QCOMPARE(window_->current_tab()->path(), a);
}

void TestMainWindow::closed_tabs_cap_is_ten() {
  const QDir dir(dir_->path());
  QString first;
  QString last;
  for (int i = 0; i < 11; ++i) {
    const QString p = write_file(dir, QStringLiteral("cap%1.md").arg(i), QStringLiteral("x\n"));
    if (i == 0) first = p;
    if (i == 10) last = p;
    QVERIFY(window_->open_file(p));
    QVERIFY(window_->close_tab(0));
  }
  QCOMPARE(window_->closed_tabs().size(), 10);
  QCOMPARE(window_->closed_tabs().first(), last);
  QVERIFY(!window_->closed_tabs().contains(first));
}

void TestMainWindow::window_close_writes_open_tabs_and_restore_reads_them() {
  const QDir dir(dir_->path());
  const QString tabs_file = dir.filePath(QStringLiteral("open-tabs"));
  const QString a = write_file(dir, QStringLiteral("wa.md"), QStringLiteral("# A\n"));
  const QString b = write_file(dir, QStringLiteral("wb.md"), QStringLiteral("# B\n"));

  auto win1 = std::make_unique<MainWindow>(dir.filePath(QStringLiteral("settings1.ini")), tabs_file);
  QVERIFY(win1->open_file(a));  
  win1->new_tab();              
  win1->tabs_for_tests()->setCurrentIndex(0);  
  QVERIFY(win1->open_file(b));  
  QCOMPARE(win1->tab_count(), 3);
  QCOMPARE(win1->current_tab()->path(), b);
  win1->close();
  win1.reset();

  MainWindow win2(dir.filePath(QStringLiteral("settings2.ini")), tabs_file);
  win2.restore_tabs();
  QCOMPARE(win2.tab_count(), 2);
  QCOMPARE(win2.tab_at(0)->path(), a);
  QCOMPARE(win2.tab_at(1)->path(), b);
  QCOMPARE(win2.current_tab()->path(), b);
}

void TestMainWindow::restore_tabs_skips_missing_file() {
  const QDir dir(dir_->path());
  const QString tabs_file = dir.filePath(QStringLiteral("open-tabs-miss"));
  const QString a = write_file(dir, QStringLiteral("ma.md"), QStringLiteral("# A\n"));
  const QString b = write_file(dir, QStringLiteral("mb.md"), QStringLiteral("# B\n"));
  const QString missing = dir.filePath(QStringLiteral("gone.md"));
  QVERIFY(write_open_tabs(tabs_file, {a, missing, b}, b));

  MainWindow win(dir.filePath(QStringLiteral("settings3.ini")), tabs_file);
  win.restore_tabs();
  QCOMPARE(win.tab_count(), 2);
  QCOMPARE(win.tab_at(0)->path(), a);
  QCOMPARE(win.tab_at(1)->path(), b);
}

void TestMainWindow::restore_tabs_all_missing_leaves_blank_tab() {
  const QDir dir(dir_->path());
  const QString tabs_file = dir.filePath(QStringLiteral("open-tabs-none"));
  const QString missing = dir.filePath(QStringLiteral("gone2.md"));
  QVERIFY(write_open_tabs(tabs_file, {missing}, QString()));

  MainWindow win(dir.filePath(QStringLiteral("settings4.ini")), tabs_file);
  win.restore_tabs();
  QCOMPARE(win.tab_count(), 1);
  QVERIFY(win.current_tab()->is_blank());
}

void TestMainWindow::tab_changes_write_open_tabs() {
  const QDir dir(dir_->path());
  const QString tabs_file = dir.filePath(QStringLiteral("open-tabs-live"));
  const QString a = write_file(dir, QStringLiteral("la.md"), QStringLiteral("# A\n"));
  const QString b = write_file(dir, QStringLiteral("lb.md"), QStringLiteral("# B\n"));
  MainWindow win(dir.filePath(QStringLiteral("settings5.ini")), tabs_file);
  auto* tabs = win.tabs_for_tests();

  QVERIFY(win.open_file(a));
  QVERIFY(win.open_file(b));
  QVERIFY(QTest::qWaitFor([&] { return read_open_tabs(tabs_file).paths == (QStringList{a, b}); }, 5000));
  QCOMPARE(read_open_tabs(tabs_file).paths, (QStringList{a, b}));
  QCOMPARE(read_open_tabs(tabs_file).active, b);

  tabs->setCurrentIndex(0);
  QVERIFY(QTest::qWaitFor([&] { return read_open_tabs(tabs_file).active == a; }, 5000));
  QCOMPARE(read_open_tabs(tabs_file).active, a);

  tabs->findChild<QTabBar*>()->moveTab(0, 1);
  QVERIFY(QTest::qWaitFor([&] { return read_open_tabs(tabs_file).paths == (QStringList{b, a}); }, 5000));
  QCOMPARE(read_open_tabs(tabs_file).paths, (QStringList{b, a}));

  QVERIFY(win.close_tab(0));
  QVERIFY(QTest::qWaitFor([&] { return read_open_tabs(tabs_file).paths == QStringList{a}; }, 5000));
  QCOMPARE(read_open_tabs(tabs_file).paths, QStringList{a});
}

void TestMainWindow::relative_path_stored_absolute() {
  const QDir dir(dir_->path());
  const QString a = write_file(dir, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  QVERIFY(window_->open_file(QDir::current().relativeFilePath(a)));
  QCOMPARE(window_->current_tab()->path(), QFileInfo(a).absoluteFilePath());
}

void TestMainWindow::corner_buttons_in_bar_middle() {
  window_->show();
  QVERIFY(QTest::qWaitForWindowExposed(window_.get()));
  QTabWidget* tabs = tab_widget();
  QWidget* corner = tabs->cornerWidget(Qt::TopRightCorner);
  QVERIFY(corner != nullptr);
  const QRect bar(tabs->tabBar()->mapTo(window_.get(), QPoint(0, 0)), tabs->tabBar()->size());
  for (QToolButton* b : corner->findChildren<QToolButton*>()) {
    const QRect r(b->mapTo(window_.get(), QPoint(0, 0)), b->size());
    QVERIFY2(r.height() >= b->sizeHint().height(), "button squeezed");
    QVERIFY2(std::abs(r.center().y() - bar.center().y()) <= 2, "button not in the middle of the bar");
  }
}

void TestMainWindow::paste_html_enabled_only_with_html() {
  QAction* action = find_action(QStringLiteral("Paste from HTML"));
  QVERIFY(action != nullptr);
  set_clipboard(QString(), QStringLiteral("plain"));
  QVERIFY(!action->isEnabled());
  set_clipboard(QStringLiteral("<b>bold</b>"), QStringLiteral("bold"));
  QVERIFY(action->isEnabled());
  find_action(QStringLiteral("Read-only"))->trigger();
  QVERIFY(!action->isEnabled());
}

void TestMainWindow::paste_html_is_one_undo_step() {
  lumos::qt::SourceEditor* editor = window_->current_tab()->editor();
  set_clipboard(QStringLiteral("<ul><li>a</li><li><b>b</b></li></ul>"), QStringLiteral("a\nb"));
  find_action(QStringLiteral("Paste from HTML"))->trigger();
  QCOMPARE(editor->text(), std::string("- a\n- **b**"));
  editor->undo();
  QCOMPARE(editor->text(), std::string());
  QVERIFY(!editor->canUndo());
}

void TestMainWindow::paste_html_key_reaches_window() {
  window_->show();
  QVERIFY(QTest::qWaitForWindowExposed(window_.get()));
  window_->activateWindow();
  lumos::qt::SourceEditor* editor = window_->current_tab()->editor();
  editor->setFocus();
  QVERIFY(QTest::qWaitForWindowActive(window_.get()));
  set_clipboard(QStringLiteral("<i>x</i>"), QStringLiteral("x"));
  QTest::sendKeyEvent(QTest::Click, editor, Qt::Key_V, QString(QChar(0x16)), Qt::ControlModifier | Qt::AltModifier);
  QCOMPARE(editor->text(), std::string("*x*"));
}

void TestMainWindow::paste_html_in_code_block_is_plain_paste() {
  lumos::qt::SourceEditor* editor = window_->current_tab()->editor();
  editor->set_text("```\ncode\n```\n");
  editor->set_cursor_position(4);
  set_clipboard(QStringLiteral("<b>x</b>"), QStringLiteral("x"));
  find_action(QStringLiteral("Paste from HTML"))->trigger();
  QCOMPARE(editor->text(), std::string("```\nxcode\n```\n"));
}

void TestMainWindow::open_and_save_keep_exact_bytes() {
  const QByteArray bytes("# A\r\nb\xff\n");
  const QString a = write_bytes(QDir(dir_->path()), QStringLiteral("a.md"), bytes);
  QVERIFY(window_->open_file(a));
  DocumentTab* tab = window_->current_tab();
  QCOMPARE(editor_bytes(tab), bytes);

  tab->editor()->insert_text("x");
  const QByteArray edited = editor_bytes(tab);
  QVERIFY(edited != bytes);
  QCOMPARE(tab_widget()->tabText(0), QStringLiteral("a.md *"));
  QVERIFY(window_->isWindowModified());

  find_action(QStringLiteral("Save"))->trigger();
  QCOMPARE(read_bytes(a), edited);
  QVERIFY(!tab->editor()->is_modified());
  QCOMPARE(tab_widget()->tabText(0), QStringLiteral("a.md"));
  QVERIFY(!window_->isWindowModified());
}

void TestMainWindow::save_as_untitled_sets_path() {
  DocumentTab* tab = window_->current_tab();
  tab->editor()->insert_text("hi\n");
  const QString path = QDir(dir_->path()).filePath(QStringLiteral("new.md"));

  answer_file_dialog(path);
  find_action(QStringLiteral("Save"))->trigger();
  QCOMPARE(read_bytes(path), QByteArray("hi\n"));
  QCOMPARE(tab->path(), path);
  QCOMPARE(tab->name(), QStringLiteral("new.md"));
  QVERIFY(!tab->editor()->is_modified());
  QCOMPARE(tab_widget()->tabText(0), QStringLiteral("new.md"));
  QVERIFY(window_->windowTitle().startsWith(QStringLiteral("new.md")));
}

void TestMainWindow::save_as_cancel_keeps_untitled() {
  DocumentTab* tab = window_->current_tab();
  tab->editor()->insert_text("hi\n");

  answer_file_dialog(QString());
  find_action(QStringLiteral("Save As..."))->trigger();
  QVERIFY(tab->path().isEmpty());
  QVERIFY(tab->editor()->is_modified());
  QVERIFY(QDir(dir_->path()).entryList({QStringLiteral("*.md")}, QDir::Files).isEmpty());
}

void TestMainWindow::save_as_onto_other_tab_refused() {
  const QDir dir(dir_->path());
  QVERIFY(dir.mkdir(QStringLiteral("sub")));
  const QString a = write_file(dir, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QString b = write_file(dir, QStringLiteral("b.md"), QStringLiteral("# B\n"));
  QVERIFY(window_->open_file(a));
  QVERIFY(window_->open_file(b));
  DocumentTab* tab = window_->current_tab();
  tab->editor()->insert_text("x");

  click_dialogs({QMessageBox::Yes, QMessageBox::Ok});
  answer_file_dialog(dir.path() + QStringLiteral("/sub/../a.md"));
  find_action(QStringLiteral("Save As..."))->trigger();
  QCOMPARE(read_bytes(a), QByteArray("# A\n"));
  QCOMPARE(read_bytes(b), QByteArray("# B\n"));
  QCOMPARE(window_->tab_count(), 2);
  QCOMPARE(window_->tab_at(0)->path(), a);
  QCOMPARE(tab->path(), b);
  QVERIFY(tab->editor()->is_modified());

  click_dialogs({QMessageBox::Yes});
  answer_file_dialog(b);
  find_action(QStringLiteral("Save As..."))->trigger();
  QCOMPARE(read_bytes(b), editor_bytes(tab));
  QVERIFY(!tab->editor()->is_modified());
}

void TestMainWindow::save_as_onto_deleted_file_of_other_tab_refused() {
  const QDir dir(dir_->path());
  const QString a = write_file(dir, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QString b = write_file(dir, QStringLiteral("b.md"), QStringLiteral("# B\n"));
  QVERIFY(window_->open_file(a));
  QVERIFY(window_->open_file(b));
  QVERIFY(QFile::remove(a));

  click_dialog(QMessageBox::Ok);
  answer_file_dialog(a);
  find_action(QStringLiteral("Save As..."))->trigger();
  QVERIFY(!QFile::exists(a));
  QCOMPARE(window_->current_tab()->path(), b);
}

void TestMainWindow::open_missing_file_shows_dialog_adds_no_tab() {
  const QDir dir(dir_->path());
  QString text;
  click_dialog_text(QStringLiteral("OK"), &text);
  QVERIFY(!window_->open_file(dir.filePath(QStringLiteral("missing.md"))));
  QVERIFY(text.startsWith(QStringLiteral("Cannot open")));
  QCOMPARE(window_->tab_count(), 1);
  QVERIFY(window_->current_tab()->is_blank());

  const QString a = write_file(dir, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  QVERIFY(window_->open_file(a));
  QCOMPARE(window_->tab_count(), 1);

  click_dialog(QMessageBox::Ok);
  QVERIFY(!window_->open_file(dir.filePath(QStringLiteral("missing.md"))));
  QCOMPARE(window_->tab_count(), 1);
  QCOMPARE(window_->current_tab()->path(), a);
}

void TestMainWindow::open_same_file_other_spelling_activates() {
  const QDir dir(dir_->path());
  QVERIFY(dir.mkdir(QStringLiteral("sub")));
  const QString a = write_file(dir, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QString b = write_file(dir, QStringLiteral("b.md"), QStringLiteral("# B\n"));
  QVERIFY(window_->open_file(a));
  QVERIFY(window_->open_file(b));
  QVERIFY(window_->open_file(dir.path() + QStringLiteral("/sub/../a.md")));
  QCOMPARE(window_->tab_count(), 2);
  QCOMPARE(window_->current_tab()->path(), a);
}

void TestMainWindow::close_modified_tab_save_writes_then_closes() {
  const QString a = write_file(QDir(dir_->path()), QStringLiteral("a.md"), QStringLiteral("# A\n"));
  QVERIFY(window_->open_file(a));
  DocumentTab* tab = window_->current_tab();
  tab->editor()->insert_text("x");
  const QByteArray edited = editor_bytes(tab);

  click_dialog(QMessageBox::Save);
  QVERIFY(window_->close_tab(0));
  QCOMPARE(read_bytes(a), edited);
  QCOMPARE(window_->tab_count(), 1);
  QVERIFY(window_->current_tab()->is_blank());
  QCOMPARE(window_->closed_tabs(), QStringList{a});
}

void TestMainWindow::quit_asks_in_tab_order_cancel_stops() {
  const QDir dir(dir_->path());
  const QString a = write_file(dir, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QString b = write_file(dir, QStringLiteral("b.md"), QStringLiteral("# B\n"));
  const QString c = write_file(dir, QStringLiteral("c.md"), QStringLiteral("# C\n"));
  QVERIFY(window_->open_file(a));
  QVERIFY(window_->open_file(b));
  QVERIFY(window_->open_file(c));
  window_->tab_at(0)->editor()->insert_text("x");
  window_->tab_at(2)->editor()->insert_text("z");
  tab_widget()->setCurrentIndex(1);

  auto seen = std::make_shared<QStringList>();
  auto answers = std::make_shared<QList<QMessageBox::StandardButton>>(
      QList<QMessageBox::StandardButton>{QMessageBox::Discard, QMessageBox::Cancel});
  auto* timer = new QTimer();
  timer->setInterval(10);
  MainWindow* window = window_.get();
  QObject::connect(timer, &QTimer::timeout, [timer, seen, answers, window] {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (box == nullptr || answers->isEmpty()) return;
    *seen << window->current_tab()->path();
    box->button(answers->takeFirst())->click();
    if (answers->isEmpty()) {
      timer->stop();
      timer->deleteLater();
    }
  });
  timer->start();

  QVERIFY(!window_->close());
  QCOMPARE(*seen, (QStringList{a, c}));
  QCOMPARE(window_->tab_count(), 3);
  QCOMPARE(window_->current_tab()->path(), c);
  QVERIFY(window_->tab_at(0)->editor()->is_modified());
  QVERIFY(window_->tab_at(2)->editor()->is_modified());
}

void TestMainWindow::revert_loads_saved_text() {
  const QString a = write_file(QDir(dir_->path()), QStringLiteral("a.md"), QStringLiteral("# A\n"));
  QAction* revert = find_action(QStringLiteral("Revert"));
  QVERIFY(!revert->isEnabled());
  window_->current_tab()->editor()->insert_text("x");
  QVERIFY(!revert->isEnabled());  
  window_->new_tab();
  QVERIFY(window_->open_file(a));
  DocumentTab* tab = window_->current_tab();
  QCOMPARE(tab->path(), a);
  QVERIFY(!revert->isEnabled());
  tab->editor()->insert_text("y");
  QVERIFY(revert->isEnabled());
  const QByteArray edited = editor_bytes(tab);

  click_dialog(QMessageBox::Cancel);
  revert->trigger();
  QCOMPARE(editor_bytes(tab), edited);

  click_dialog(QMessageBox::Discard);
  revert->trigger();
  QCOMPARE(editor_bytes(tab), QByteArray("# A\n"));
  QVERIFY(!tab->editor()->is_modified());
  QCOMPARE(read_bytes(a), QByteArray("# A\n"));
}

void TestMainWindow::window_menu_lists_tabs_and_switches() {
  const QDir dir(dir_->path());
  const QString a = write_file(dir, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QString b = write_file(dir, QStringLiteral("b.md"), QStringLiteral("# B\n"));
  QVERIFY(window_->open_file(a));
  QVERIFY(window_->open_file(b));
  window_->new_tab();

  QMenu* menu = nullptr;
  for (QMenu* m : window_->findChildren<QMenu*>())
    if (QString(m->title()).remove(QLatin1Char('&')) == QStringLiteral("Window")) menu = m;
  QVERIFY(menu != nullptr);
  const auto tab_items = [&] {
    emit menu->aboutToShow();
    const QList<QAction*> all = menu->actions();
    return all.mid(all.size() - window_->tab_count());
  };

  QList<QAction*> items = tab_items();
  QCOMPARE(items.size(), 3);
  QCOMPARE(items[0]->text(), QStringLiteral("a.md"));
  QCOMPARE(items[1]->text(), QStringLiteral("b.md"));
  QCOMPARE(items[2]->text(), QStringLiteral("Untitled"));
  QVERIFY(!items[0]->isChecked() && !items[1]->isChecked() && items[2]->isChecked());

  items[0]->trigger();
  QCOMPARE(tab_widget()->currentIndex(), 0);

  tab_widget()->findChild<QTabBar*>()->moveTab(0, 2);
  items = tab_items();
  QCOMPARE(items[0]->text(), QStringLiteral("b.md"));
  QCOMPARE(items[2]->text(), QStringLiteral("a.md"));
  QVERIFY(items[2]->isChecked());
}

void TestMainWindow::list_buffers_in_tab_order_with_ids() {
  const QDir dir(dir_->path());
  const QString a = write_file(dir, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QString b = write_file(dir, QStringLiteral("b.md"), QStringLiteral("# B\n"));
  QList<BufferInfo> list = window_->list_buffers();
  QCOMPARE(list.size(), 1);
  QCOMPARE(list[0].id, quint64{1});
  QVERIFY(list[0].path.isEmpty());
  QCOMPARE(list[0].name, QStringLiteral("Untitled"));
  QVERIFY(!list[0].modified && list[0].current);

  QVERIFY(window_->open_file(a));  
  QVERIFY(window_->open_file(b));
  window_->new_tab();
  window_->tab_at(1)->editor()->insert_text("x");
  list = window_->list_buffers();
  QCOMPARE(list.size(), 3);
  QCOMPARE(list[0].id, quint64{1});
  QCOMPARE(list[0].path, a);
  QCOMPARE(list[0].name, QStringLiteral("a.md"));
  QCOMPARE(list[1].id, quint64{2});
  QCOMPARE(list[1].path, b);
  QVERIFY(list[1].modified && !list[1].current);
  QCOMPARE(list[2].id, quint64{3});
  QVERIFY(list[2].path.isEmpty());
  QVERIFY(!list[2].modified && list[2].current);
  QVERIFY(!list[0].modified && !list[0].current);

  tab_widget()->findChild<QTabBar*>()->moveTab(0, 2);
  list = window_->list_buffers();
  QCOMPARE(list[0].id, quint64{2});
  QCOMPARE(list[2].id, quint64{1});
  QCOMPARE(list[2].path, a);

  const QString c = dir.filePath(QStringLiteral("c.md"));
  DocumentTab* untitled = window_->tab_at(1);
  QString error;
  QVERIFY(untitled->write(c, &error));
  untitled->set_path(c);
  list = window_->list_buffers();
  QCOMPARE(list[1].id, quint64{3});
  QCOMPARE(list[1].path, c);

  window_->tab_at(0)->editor()->undo();
  QVERIFY(window_->close_tab(0));  
  QVERIFY(window_->open_file(b));
  list = window_->list_buffers();
  QCOMPARE(list.size(), 3);
  for (const BufferInfo& info : list)
    if (info.path == b) QCOMPARE(info.id, quint64{4});
}

void TestMainWindow::read_buffer_gives_exact_bytes() {
  const QDir dir(dir_->path());
  const QByteArray bytes("# A\r\nb\xff\n");
  const QString a = write_bytes(dir, QStringLiteral("a.md"), bytes);
  const QString b = write_file(dir, QStringLiteral("b.md"), QStringLiteral("# B\n"));
  QVERIFY(window_->open_file(a));
  QVERIFY(window_->open_file(b));
  const quint64 id_a = window_->tab_at(0)->buffer_id();
  const quint64 id_b = window_->tab_at(1)->buffer_id();

  QCOMPARE(window_->read_buffer(id_a), std::optional<QByteArray>(bytes));
  QCOMPARE(tab_widget()->currentIndex(), 1);
  window_->tab_at(1)->editor()->insert_text("x");
  QCOMPARE(window_->read_buffer(id_b), std::optional<QByteArray>(editor_bytes(window_->tab_at(1))));
  QCOMPARE(read_bytes(b), QByteArray("# B\n"));

  QVERIFY(!window_->read_buffer(0).has_value());
  QVERIFY(!window_->read_buffer(99).has_value());
  QVERIFY(window_->close_tab(0));
  QVERIFY(!window_->read_buffer(id_a).has_value());
}

void TestMainWindow::find_buffer_by_any_spelling() {
  const QDir dir(dir_->path());
  QVERIFY(dir.mkdir(QStringLiteral("sub")));
  const QString a = write_file(dir, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QString b = write_file(dir, QStringLiteral("b.md"), QStringLiteral("# B\n"));
  const QString link = dir.filePath(QStringLiteral("a_link.md"));
  QVERIFY(QFile::link(a, link));
  QVERIFY(window_->open_file(a));
  window_->new_tab();
  const quint64 id_a = window_->tab_at(0)->buffer_id();

  QCOMPARE(window_->find_buffer(a), std::optional<quint64>(id_a));
  QCOMPARE(window_->find_buffer(link), std::optional<quint64>(id_a));
  QCOMPARE(window_->find_buffer(dir.path() + QStringLiteral("/sub/../a.md")), std::optional<quint64>(id_a));
  QVERIFY(!window_->find_buffer(b).has_value());
  QVERIFY(!window_->find_buffer(dir.filePath(QStringLiteral("missing.md"))).has_value());
  QVERIFY(!window_->find_buffer(QString()).has_value());
  QCOMPARE(tab_widget()->currentIndex(), 1);
}

void TestMainWindow::read_only_file_save_opens_save_as() {
  if (!file_modes_apply()) QSKIP("file modes do not apply (root)");
  const QDir dir(dir_->path());
  const QString a = write_file(dir, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  QVERIFY(QFile::setPermissions(a, QFileDevice::ReadOwner));
  QVERIFY(window_->open_file(a));
  DocumentTab* tab = window_->current_tab();
  QVERIFY(tab->read_only_file());
  QVERIFY(window_->windowTitle().contains(QStringLiteral("[Read-Only]")));
  QVERIFY(!tab->editor()->is_read_only());

  tab->editor()->insert_text("x");
  answer_file_dialog(QString());
  find_action(QStringLiteral("Save"))->trigger();
  QCOMPARE(read_bytes(a), QByteArray("# A\n"));
  QVERIFY(tab->editor()->is_modified());

  const QString b = dir.filePath(QStringLiteral("b.md"));
  answer_file_dialog(b);
  find_action(QStringLiteral("Save"))->trigger();
  QCOMPARE(read_bytes(a), QByteArray("# A\n"));
  QCOMPARE(read_bytes(b), editor_bytes(tab));
  QCOMPARE(tab->path(), b);
  QVERIFY(!tab->read_only_file());
  QVERIFY(!window_->windowTitle().contains(QStringLiteral("[Read-Only]")));
}

void TestMainWindow::read_only_file_cleared_by_chmod() {
  if (!file_modes_apply()) QSKIP("file modes do not apply (root)");
  const QDir dir(dir_->path());
  const QString a = write_file(dir, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  QVERIFY(QFile::setPermissions(a, QFileDevice::ReadOwner));
  QVERIFY(window_->open_file(a));
  DocumentTab* tab = window_->current_tab();
  QVERIFY(tab->read_only_file());

  QVERIFY(QFile::setPermissions(a, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
  window_->check_files();
  QVERIFY(!tab->read_only_file());
  QVERIFY(!window_->windowTitle().contains(QStringLiteral("[Read-Only]")));
  tab->editor()->insert_text("x");
  find_action(QStringLiteral("Save"))->trigger();
  QCOMPARE(read_bytes(a), editor_bytes(tab));
}

void TestMainWindow::read_only_dir_makes_file_read_only() {
  if (!file_modes_apply()) QSKIP("file modes do not apply (root)");
  const QDir dir(dir_->path());
  QVERIFY(dir.mkdir(QStringLiteral("ro")));
  const QString sub = dir.filePath(QStringLiteral("ro"));
  const QString a = write_file(QDir(sub), QStringLiteral("a.md"), QStringLiteral("# A\n"));
  QVERIFY(QFile::setPermissions(sub, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
  const auto restore = qScopeGuard([&] {
    QFile::setPermissions(sub, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
  });
  QVERIFY(window_->open_file(a));
  QVERIFY(window_->current_tab()->read_only_file());
  QVERIFY(window_->windowTitle().contains(QStringLiteral("[Read-Only]")));
}

void TestMainWindow::theme_menu_applies_and_saves() {
  QAction* gruvbox = find_action(QStringLiteral("Gruvbox"));
  QVERIFY(gruvbox != nullptr);
  QVERIFY(find_action(QStringLiteral("Default"))->isChecked());
  gruvbox->trigger();
  QVERIFY(gruvbox->isChecked());
  QCOMPARE(QApplication::palette().color(QPalette::Window), QColor(0x1d, 0x20, 0x21));
  QCOMPARE(window_->current_tab()->editor()->style_table().page_background.color(), QColor(0x32, 0x30, 0x2f));
  QVERIFY(qApp->styleSheet().contains(QStringLiteral("#282828")));
  const QString settings_file = QDir(dir_->path()).filePath(QStringLiteral("settings.ini"));
  QCOMPARE(QSettings(settings_file, QSettings::IniFormat).value(QStringLiteral("view/theme")).toString(),
           QStringLiteral("gruvbox"));

  window_.reset();
  window_ = std::make_unique<MainWindow>(settings_file);
  QVERIFY(find_action(QStringLiteral("Gruvbox"))->isChecked());
  QCOMPARE(QApplication::palette().color(QPalette::Window), QColor(0x1d, 0x20, 0x21));
}

void TestMainWindow::theme_default_gives_system_palette_back() {
  const QPalette start = QApplication::palette();
  find_action(QStringLiteral("Paper look"))->trigger();
  const QBrush paper = window_->current_tab()->editor()->style_table().outside_background;
  find_action(QStringLiteral("Light grey"))->trigger();
  QCOMPARE(QApplication::palette().color(QPalette::Base), QColor(0xd9, 0xd9, 0xd9));
  QVERIFY(window_->current_tab()->editor()->style_table().outside_background != paper);
  find_action(QStringLiteral("Default"))->trigger();
  for (const QPalette::ColorRole role : {QPalette::Window, QPalette::Base, QPalette::Text, QPalette::Highlight})
    QCOMPARE(QApplication::palette().color(role), start.color(role));
  QCOMPARE(window_->current_tab()->editor()->style_table().outside_background, paper);
  QVERIFY(qApp->styleSheet().contains(start.color(QPalette::Base).name()));
}

void TestMainWindow::theme_unknown_id_and_unknown_role() {
  QCOMPARE(find_theme(QStringLiteral("solarized")).id, std::string_view("default"));
  QCOMPARE(find_theme(QString()).id, std::string_view("default"));
  QStringList unknown;
  const QString out =
      fill_stylesheet(u"a {{fg_primary}} b {{no_role}}", find_theme(QStringLiteral("gruvbox")), QPalette(), &unknown);
  QCOMPARE(out, QStringLiteral("a #ebdbb2 b {{no_role}}"));
  QCOMPARE(unknown, QStringList{QStringLiteral("no_role")});
  QStringList none;
  for (const Theme& t : themes()) fill_stylesheet(theme_stylesheet(t, QPalette()), t, QPalette(), &none);
  QVERIFY(none.isEmpty());
}

void TestMainWindow::theme_png() {
  if (!qEnvironmentVariableIsSet("LUMOS_THEME_PNG")) return;
  const QDir out(qEnvironmentVariable("LUMOS_THEME_PNG"));
  const QDir dir(dir_->path());
  QVERIFY(window_->open_file(write_file(dir, QStringLiteral("notes.md"), QStringLiteral("# Notes\n\nSome text.\n"))));
  QVERIFY(window_->open_file(
      write_file(dir, QStringLiteral("todo.md"), QStringLiteral("# To do\n\n- [ ] **one**\n- [x] `two`\n\n> quote\n"))));
  window_->resize(1100, 450);
  window_->show();
  QVERIFY(QTest::qWaitForWindowExposed(window_.get()));
  find_action(QStringLiteral("Show Preview"))->trigger();
  find_action(QStringLiteral("Replace..."))->trigger();
  for (const Theme& t : themes()) {
    find_action(QString::fromUtf8(t.name.data(), static_cast<qsizetype>(t.name.size())))->trigger();
    QCoreApplication::processEvents();
    QVERIFY(window_->grab().save(out.filePath(QString::fromLatin1(t.id.data(), static_cast<qsizetype>(t.id.size())) + QStringLiteral(".png"))));
  }
}

void TestMainWindow::tab_close_button_closes_its_tab() {
  const QDir dir(dir_->path());
  const QString a = write_file(dir, QStringLiteral("a.md"), QStringLiteral("a\n"));
  const QString b = write_file(dir, QStringLiteral("b.md"), QStringLiteral("b\n"));
  const QString c = write_file(dir, QStringLiteral("c.md"), QStringLiteral("c\n"));
  QVERIFY(window_->open_file(a));
  QVERIFY(window_->open_file(b));
  QVERIFY(window_->open_file(c));
  QTabBar* bar = tab_widget()->tabBar();
  bar->moveTab(0, 2);  
  auto* close = qobject_cast<QToolButton*>(bar->tabButton(2, QTabBar::RightSide));
  QVERIFY(close != nullptr);
  QCOMPARE(close->objectName(), QStringLiteral("tab_close"));
  close->click();
  QCOMPARE(window_->tab_count(), 2);
  QCOMPARE(window_->tab_at(0)->path(), b);
  QCOMPARE(window_->tab_at(1)->path(), c);
  QCOMPARE(window_->current_tab()->path(), c);
}

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
