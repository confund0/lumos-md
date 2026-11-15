
#include <QAction>
#include <QDir>
#include <QFile>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <memory>

#include "document_tab.hpp"
#include "main_window.hpp"
#include "open_tabs.hpp"
#include "lumos/qt/source_editor.hpp"

namespace {

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

 private:
  QAction* find_action(const QString& text) const;
  QTabWidget* tab_widget() const { return qobject_cast<QTabWidget*>(window_->centralWidget()); }

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
  qobject_cast<QTabWidget*>(win1->centralWidget())->setCurrentIndex(0);  
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

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
