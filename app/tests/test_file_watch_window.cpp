
#include <QAction>
#include <QAbstractButton>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWidget>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "dialog_util.hpp"
#include "file_modes.hpp"
#include "document_tab.hpp"
#include "line_merge.hpp"
#include "lumos/log.hpp"
#include "lumos/qt/source_editor.hpp"
#include "main_window.hpp"
#include "merge_window.hpp"

namespace {

QString write_file(const QDir& dir, const QString& name, const QByteArray& bytes) {
  const QString path = dir.filePath(name);
  QFile f(path);
  const bool ok = f.open(QIODevice::WriteOnly);
  Q_ASSERT(ok);
  Q_UNUSED(ok);
  f.write(bytes);
  f.close();
  return path;
}

void rewrite_in_place(const QString& path, const QByteArray& bytes) {
  QFile f(path);
  const bool ok = f.open(QIODevice::WriteOnly | QIODevice::Truncate);
  Q_ASSERT(ok);
  Q_UNUSED(ok);
  f.write(bytes);
  f.close();
}

void rewrite_by_rename(const QString& path, const QByteArray& bytes) {
  QSaveFile f(path);
  const bool ok = f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
  Q_ASSERT(ok);
  Q_UNUSED(ok);
}

QLabel* notice_label(MainWindow& window) { return window.findChild<QLabel*>(QStringLiteral("notice")); }

QTabWidget* tab_widget(MainWindow& window) { return window.tabs_for_tests(); }

QString draft_file(const QString& drafts_dir, const QString& id) {
  return QDir(drafts_dir).filePath(id + QStringLiteral(".draft"));
}

void trigger_save(MainWindow& window) {
  for (QAction* a : window.findChildren<QAction*>()) {
    if (QString(a->text()).remove(QLatin1Char('&')) == QStringLiteral("Save")) {
      a->trigger();
      return;
    }
  }
  QFAIL("Save action not found");
}

void make_active(MainWindow& window) {
  window.show();
  window.activateWindow();
  const bool ok = QTest::qWaitForWindowActive(&window);
  Q_ASSERT(ok);
  Q_UNUSED(ok);
}

void handle_merge_window(std::optional<std::vector<Pick>> picks, std::shared_ptr<int> conflicts = nullptr) {
  auto* timer = new QTimer();
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [timer, picks, conflicts] {
    auto* window = dynamic_cast<MergeWindow*>(QApplication::activeModalWidget());
    if (window == nullptr) return;
    if (conflicts != nullptr) *conflicts = window->conflicts();
    if (picks.has_value()) {
      for (Pick p : *picks) window->pick(p);
      QTest::mouseClick(window->use_button(), Qt::LeftButton);
    } else {
      window->reject();
    }
    timer->stop();
    timer->deleteLater();
  });
  timer->start();
}

void check_button_enabled_then_click(const QString& button_text, bool* enabled, const QString& another_button_text) {
  auto* timer = new QTimer();
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [timer, button_text, enabled, another_button_text] {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (box == nullptr) return;
    for (QAbstractButton* button : box->buttons()) {
      if (button->text() == button_text) *enabled = button->isEnabled();
      if (button->text() == another_button_text) button->click();
    }
    timer->stop();
    timer->deleteLater();
  });
  timer->start();
}

QByteArray distinct_lines(const QString& kind, int n) {
  QByteArray out;
  for (int i = 0; i < n; ++i) out += QStringLiteral("%1%2\n").arg(kind).arg(i).toUtf8();
  return out;
}

}  

class TestFileWatchWindow : public QObject {
  Q_OBJECT

 private slots:
  void rewrite_in_place_reloads();
  void rewrite_by_rename_reloads();
  void touch_same_bytes_no_reload();
  void own_save_no_reload();
  void non_current_tab_waits_then_reloads_on_switch();
  void inactive_window_waits_then_reloads_on_activation();
  void rename_save_then_outside_write_still_seen();
  void warning_notice_has_the_amber_background();
  void modified_tab_outside_change_cancel();
  void escape_on_changed_dialog_means_cancel();
  void modified_tab_outside_change_load_changes();
  void save_checks_disk_before_save_as_shown();
  void gone_unmodified_keep_in_editor();
  void gone_close_closes_tab();
  void gone_and_back_same_bytes_no_dialog();
  void pending_change_waits_for_other_dialog();
  void watch_save_as_shown_writes_tab_text();
  void watch_read_only_file_disables_save_as_shown();
  void watch_save_as_shown_file_changed_again_shows_dialog_again();
  void watch_show_diff_use_result_merges_and_undoes();
  void watch_show_diff_cancel_shows_dialog_again_then_cancel_keeps();
  void watch_use_result_equal_to_disk_not_modified();
  void save_after_load_changes_returns_false_and_no_write();
  void save_write_failure_keeps_tab();
  void watch_too_many_disables_show_diff();
  void base_follows_saves_no_conflict_after_second_save();
  void disk_merge_use_log_has_no_document_text();
};

void TestFileWatchWindow::rewrite_in_place_reloads() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  QVERIFY(!tab->editor()->is_modified());

  rewrite_in_place(path, QByteArray("# B\n"));
  QVERIFY(QTest::qWaitFor([&] { return tab->editor()->text() == std::string("# B\n"); }, 2000));
  QVERIFY(!tab->editor()->is_modified());
  QLabel* notice = notice_label(window);
  QVERIFY(notice != nullptr);
  QVERIFY(notice->isVisible());
  QVERIFY(notice->text().contains(QStringLiteral("↻")));  
  QVERIFY(notice->text().contains(QStringLiteral("a.md")));
  QCOMPARE(notice->palette().color(QPalette::Window), notice->palette().color(QPalette::Highlight));

  tab->editor()->undo();
  QCOMPARE(tab->editor()->text(), std::string("# A\n"));
  QVERIFY(tab->editor()->is_modified());
}

void TestFileWatchWindow::rewrite_by_rename_reloads() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();

  rewrite_by_rename(path, QByteArray("# B\n"));
  QVERIFY(QTest::qWaitFor([&] { return tab->editor()->text() == std::string("# B\n"); }, 2000));
  QVERIFY(!tab->editor()->is_modified());
}

void TestFileWatchWindow::touch_same_bytes_no_reload() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  const bool could_undo_before = tab->editor()->canUndo();

  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadWrite));
  QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(5), QFileDevice::FileModificationTime));
  f.close();

  window.check_files();
  QTest::qWait(50);
  QCOMPARE(tab->editor()->canUndo(), could_undo_before);
  QLabel* notice = notice_label(window);
  QVERIFY(notice == nullptr || !notice->isVisible());
}

void TestFileWatchWindow::own_save_no_reload() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  tab->editor()->insert_text("x");
  QVERIFY(tab->write(path, nullptr));
  const bool could_undo_before = tab->editor()->canUndo();

  window.check_files();
  QTest::qWait(50);
  QCOMPARE(tab->editor()->canUndo(), could_undo_before);
  QLabel* notice = notice_label(window);
  QVERIFY(notice == nullptr || !notice->isVisible());
}

void TestFileWatchWindow::non_current_tab_waits_then_reloads_on_switch() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  window.new_tab();
  QVERIFY(window.current_tab() != tab);

  rewrite_in_place(path, QByteArray("# B\n"));
  window.check_files();
  QTest::qWait(50);
  QCOMPARE(tab->editor()->text(), std::string("# A\n"));

  window.step_tab(1);  
  QCOMPARE(window.current_tab(), tab);
  QVERIFY(QTest::qWaitFor([&] { return tab->editor()->text() == std::string("# B\n"); }, 2000));
}

void TestFileWatchWindow::inactive_window_waits_then_reloads_on_activation() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();

  QWidget other;
  other.show();
  other.activateWindow();
  if (!QTest::qWaitForWindowActive(&other) || window.isActiveWindow()) {
    QSKIP("offscreen did not move activation to the other widget here");
  }

  rewrite_in_place(path, QByteArray("# B\n"));
  window.check_files();
  QTest::qWait(50);
  QCOMPARE(tab->editor()->text(), std::string("# A\n"));

  window.activateWindow();
  QVERIFY(QTest::qWaitForWindowActive(&window));
  QVERIFY(QTest::qWaitFor([&] { return tab->editor()->text() == std::string("# B\n"); }, 2000));
}

void TestFileWatchWindow::rename_save_then_outside_write_still_seen() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();

  rewrite_by_rename(path, QByteArray("# B\n"));
  QVERIFY(QTest::qWaitFor([&] { return tab->editor()->text() == std::string("# B\n"); }, 2000));

  rewrite_by_rename(path, QByteArray("# C\n"));
  QVERIFY(QTest::qWaitFor([&] { return tab->editor()->text() == std::string("# C\n"); }, 2000));
}

void TestFileWatchWindow::warning_notice_has_the_amber_background() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString blocked_drafts_dir = write_file(d, QStringLiteral("drafts"), QByteArray("x"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), blocked_drafts_dir);
  make_active(window);
  DocumentTab* tab = window.current_tab();
  tab->editor()->insert_text("x");
  tab->write_draft_now();

  QLabel* notice = notice_label(window);
  QVERIFY(notice != nullptr);
  QVERIFY(notice->isVisible());
  QCOMPARE(notice->palette().color(QPalette::Window), QColor(0xff, 0xd5, 0x4f));
}

void TestFileWatchWindow::modified_tab_outside_change_cancel() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  tab->editor()->insert_text("x");
  QVERIFY(tab->editor()->is_modified());

  rewrite_in_place(path, QByteArray("# B\n"));
  QString text;
  QString informative;
  click_dialog_text(QStringLiteral("Cancel"), &text, &informative);
  window.check_files();
  QTest::qWait(50);
  QVERIFY(text.contains(QStringLiteral("a.md")));
  QVERIFY(text.contains(QStringLiteral("was changed on disk")));
  QVERIFY(informative.contains(QStringLiteral("Show diff")));
  QCOMPARE(tab->editor()->text(), std::string("x# A\n"));
  QVERIFY(tab->editor()->is_modified());

  auto count = std::make_shared<int>(0);
  QTimer* guard = guard_no_dialog(count);
  window.check_files();
  QTest::qWait(50);
  QCOMPARE(*count, 0);

  trigger_save(window);
  QTest::qWait(50);
  QCOMPARE(*count, 0);
  guard->stop();
  guard->deleteLater();
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  QCOMPARE(f.readAll(), QByteArray("x# A\n"));
  QVERIFY(!tab->editor()->is_modified());
}

void TestFileWatchWindow::escape_on_changed_dialog_means_cancel() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  tab->editor()->insert_text("x");

  rewrite_in_place(path, QByteArray("# B\n"));
  escape_dialog();
  window.check_files();
  QTest::qWait(50);
  QCOMPARE(tab->editor()->text(), std::string("x# A\n"));
  QVERIFY(tab->editor()->is_modified());

  auto count = std::make_shared<int>(0);
  QTimer* guard = guard_no_dialog(count);
  window.check_files();
  QTest::qWait(50);
  QCOMPARE(*count, 0);
  guard->stop();
  guard->deleteLater();
}

void TestFileWatchWindow::modified_tab_outside_change_load_changes() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  tab->editor()->insert_text("x");

  rewrite_in_place(path, QByteArray("# B\n"));
  click_dialog_text(QStringLiteral("Load changes"));
  window.check_files();
  QTest::qWait(50);
  QCOMPARE(tab->editor()->text(), std::string("# B\n"));
  QVERIFY(!tab->editor()->is_modified());
  QLabel* notice = notice_label(window);
  QVERIFY(notice != nullptr);
  QVERIFY(notice->text().contains(QStringLiteral("↻")));

  tab->editor()->undo();
  QCOMPARE(tab->editor()->text(), std::string("x# A\n"));
  QVERIFY(tab->editor()->is_modified());
}

void TestFileWatchWindow::save_checks_disk_before_save_as_shown() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  tab->editor()->insert_text("x");

  rewrite_in_place(path, QByteArray("# B\n"));
  QString text;
  click_dialog_text(QStringLiteral("Cancel"), &text);
  trigger_save(window);
  QTest::qWait(50);
  QVERIFY(text.contains(QStringLiteral("a.md")));
  QVERIFY(text.contains(QStringLiteral("changed on disk since it was opened or saved")));
  QFile f1(path);
  QVERIFY(f1.open(QIODevice::ReadOnly));
  QCOMPARE(f1.readAll(), QByteArray("# B\n"));
  QVERIFY(tab->editor()->is_modified());

  click_dialog_text(QStringLiteral("Save as shown"));
  trigger_save(window);
  QTest::qWait(50);
  QFile f2(path);
  QVERIFY(f2.open(QIODevice::ReadOnly));
  QCOMPARE(f2.readAll(), QByteArray("x# A\n"));
  QVERIFY(!tab->editor()->is_modified());
}

void TestFileWatchWindow::gone_unmodified_keep_in_editor() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  QVERIFY(!tab->editor()->is_modified());

  QVERIFY(QFile::remove(path));
  QString text;
  QString informative;
  click_dialog_text(QStringLiteral("Keep in editor"), &text, &informative);
  window.check_files();
  QTest::qWait(50);
  QVERIFY(text.contains(QStringLiteral("a.md")));
  QVERIFY(text.contains(QStringLiteral("deleted or moved")));
  QVERIFY(tab->editor()->is_modified());
  QTabWidget* tabs = tab_widget(window);
  QCOMPARE(tabs->tabText(tabs->indexOf(tab)), QStringLiteral("a.md *"));
  const QString id = tab->draft_id();
  QVERIFY(!id.isEmpty());
  QVERIFY(QFile::exists(draft_file(drafts_dir, id)));

  auto count = std::make_shared<int>(0);
  QTimer* guard = guard_no_dialog(count);
  window.check_files();
  QTest::qWait(50);
  QCOMPARE(*count, 0);
  guard->stop();
  guard->deleteLater();

  trigger_save(window);
  QTest::qWait(50);
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  QCOMPARE(f.readAll(), QByteArray("# A\n"));
  QVERIFY(!tab->editor()->is_modified());
  QVERIFY(!QFile::exists(draft_file(drafts_dir, id)));
}

void TestFileWatchWindow::gone_close_closes_tab() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  QVERIFY(!tab->editor()->is_modified());
  QCOMPARE(window.tab_count(), 1);

  const QPointer<DocumentTab> closed(tab);
  QVERIFY(QFile::remove(path));
  click_dialog_text(QStringLiteral("Close"));
  window.check_files();
  QVERIFY(QTest::qWaitFor([&] { return closed.isNull(); }, 2000));
  QCOMPARE(window.tab_count(), 1);
  QVERIFY(window.current_tab()->is_blank());
}

void TestFileWatchWindow::gone_and_back_same_bytes_no_dialog() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();

  QVERIFY(QFile::remove(path));
  write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  auto count = std::make_shared<int>(0);
  QTimer* guard = guard_no_dialog(count);
  window.check_files();
  QTest::qWait(50);
  QCOMPARE(*count, 0);
  guard->stop();
  guard->deleteLater();
  QVERIFY(!tab->editor()->is_modified());
}

void TestFileWatchWindow::pending_change_waits_for_other_dialog() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  tab->editor()->insert_text("x");

  bool blocked = false;
  QTimer::singleShot(0, &window, [&] {
    QMessageBox other(QMessageBox::Information, QStringLiteral("Other"), QStringLiteral("Other dialog"),
                       QMessageBox::Ok, &window);
    QTimer::singleShot(20, &window, [&] {
      rewrite_in_place(path, QByteArray("# B\n"));
      window.check_files();
      blocked = tab->disk_pending() == DiskState::changed && QApplication::activeModalWidget() == &other;
      other.accept();
    });
    other.exec();
  });
  QTest::qWait(100);
  QVERIFY(blocked);
  QVERIFY(QApplication::activeModalWidget() == nullptr);
  make_active(window);

  click_dialog_text(QStringLiteral("Cancel"));
  window.check_files();
  QTest::qWait(50);
  QCOMPARE(tab->editor()->text(), std::string("x# A\n"));
  QVERIFY(tab->editor()->is_modified());
}

void TestFileWatchWindow::watch_save_as_shown_writes_tab_text() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  tab->editor()->insert_text("x");

  rewrite_in_place(path, QByteArray("# B\n"));
  click_dialog_text(QStringLiteral("Save as shown"));
  window.check_files();
  QTest::qWait(100);

  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  QCOMPARE(f.readAll(), QByteArray("x# A\n"));
  QVERIFY(!tab->editor()->is_modified());
}

void TestFileWatchWindow::watch_read_only_file_disables_save_as_shown() {
  if (!file_modes_apply()) QSKIP("file modes do not apply (root)");
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  tab->editor()->insert_text("x");

  QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
  rewrite_in_place(path, QByteArray("# B\n"));
  QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner));
  auto save_enabled = std::make_shared<int>(-1);
  auto* timer = new QTimer();
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [timer, save_enabled] {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (box == nullptr) return;
    QAbstractButton* cancel = nullptr;
    for (QAbstractButton* button : box->buttons()) {
      if (button->text() == QStringLiteral("Save as shown")) *save_enabled = button->isEnabled() ? 1 : 0;
      if (button->text() == QStringLiteral("Cancel")) cancel = button;
    }
    timer->stop();
    timer->deleteLater();
    if (cancel != nullptr) cancel->click();
  });
  timer->start();
  window.check_files();
  QTest::qWait(100);

  QCOMPARE(*save_enabled, 0);
  QCOMPARE(tab->editor()->text(), std::string("x# A\n"));
  QVERIFY(tab->editor()->is_modified());
}

void TestFileWatchWindow::watch_save_as_shown_file_changed_again_shows_dialog_again() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  tab->editor()->insert_text("x");

  rewrite_in_place(path, QByteArray("# B\n"));
  auto dialogs = std::make_shared<int>(0);
  auto* timer = new QTimer();
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [timer, path, dialogs] {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (box == nullptr) return;
    ++*dialogs;
    if (*dialogs == 1) {
      rewrite_in_place(path, QByteArray("# C\n"));  
      for (QAbstractButton* button : box->buttons()) {
        if (button->text() == QStringLiteral("Save as shown")) {
          button->click();
          break;
        }
      }
    } else {
      for (QAbstractButton* button : box->buttons()) {
        if (button->text() == QStringLiteral("Cancel")) {
          button->click();
          break;
        }
      }
      timer->stop();
      timer->deleteLater();
    }
  });
  timer->start();
  window.check_files();
  QTest::qWait(150);

  QCOMPARE(*dialogs, 2);
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  QCOMPARE(f.readAll(), QByteArray("# C\n"));  
}

void TestFileWatchWindow::watch_show_diff_use_result_merges_and_undoes() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("1\n2\n3\n4\n5\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  tab->editor()->replace_text("1\nY2\n3\n4\n5\n");
  QVERIFY(tab->editor()->is_modified());
  const std::string before_merge = tab->editor()->text();

  rewrite_in_place(path, QByteArray("1\nO2\n3\nO4\n5\n"));
  click_dialog_text(QStringLiteral("Show diff"));
  handle_merge_window(std::vector<Pick>{Pick::yours});
  window.check_files();
  QTest::qWait(150);

  QCOMPARE(tab->editor()->text(), std::string("1\nY2\n3\nO4\n5\n"));
  QVERIFY(tab->editor()->is_modified());

  tab->editor()->undo();
  QCOMPARE(tab->editor()->text(), before_merge);
  tab->editor()->redo();

  auto count = std::make_shared<int>(0);
  QTimer* guard = guard_no_dialog(count);
  trigger_save(window);
  QTest::qWait(50);
  QCOMPARE(*count, 0);
  guard->stop();
  guard->deleteLater();
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  QCOMPARE(f.readAll(), QByteArray("1\nY2\n3\nO4\n5\n"));
}

void TestFileWatchWindow::watch_show_diff_cancel_shows_dialog_again_then_cancel_keeps() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("1\n2\n3\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  tab->editor()->insert_text("x");

  rewrite_in_place(path, QByteArray("1\nO2\n3\n"));
  auto dialogs = std::make_shared<int>(0);
  auto* timer = new QTimer();
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [timer, dialogs] {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (box == nullptr) return;
    ++*dialogs;
    const QString button_text = (*dialogs == 1) ? QStringLiteral("Show diff") : QStringLiteral("Cancel");
    for (QAbstractButton* button : box->buttons()) {
      if (button->text() == button_text) {
        button->click();
        break;
      }
    }
    if (*dialogs == 2) {
      timer->stop();
      timer->deleteLater();
    }
  });
  timer->start();
  handle_merge_window(std::nullopt);  
  window.check_files();
  QTest::qWait(150);

  QCOMPARE(*dialogs, 2);
  QCOMPARE(tab->editor()->text(), std::string("x1\n2\n3\n"));
  QVERIFY(tab->editor()->is_modified());

  auto count = std::make_shared<int>(0);
  QTimer* guard = guard_no_dialog(count);
  window.check_files();
  QTest::qWait(50);
  QCOMPARE(*count, 0);
  guard->stop();
  guard->deleteLater();
}

void TestFileWatchWindow::watch_use_result_equal_to_disk_not_modified() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("1\n2\n3\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  tab->editor()->replace_text("1\nY2\n3\n");
  QVERIFY(tab->editor()->is_modified());

  rewrite_in_place(path, QByteArray("1\nY2\n3\n"));
  click_dialog_text(QStringLiteral("Show diff"));
  handle_merge_window(std::vector<Pick>{});
  window.check_files();
  QTest::qWait(150);

  QCOMPARE(tab->editor()->text(), std::string("1\nY2\n3\n"));
  QVERIFY(!tab->editor()->is_modified());
}

void TestFileWatchWindow::save_after_load_changes_returns_false_and_no_write() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  tab->editor()->insert_text("x");

  rewrite_in_place(path, QByteArray("# B\n"));
  auto step = std::make_shared<int>(0);
  auto* timer = new QTimer();
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [timer, step] {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (box == nullptr) return;
    if (*step == 0) {
      QAbstractButton* button = box->button(QMessageBox::Save);
      if (button != nullptr) button->click();
      *step = 1;
    } else {
      for (QAbstractButton* button : box->buttons()) {
        if (button->text() == QStringLiteral("Load changes")) {
          button->click();
          break;
        }
      }
      timer->stop();
      timer->deleteLater();
    }
  });
  timer->start();
  const bool closed = window.close_tab(0);
  QTest::qWait(150);

  QVERIFY(!closed);
  QCOMPARE(tab->editor()->text(), std::string("# B\n"));
  QVERIFY(!tab->editor()->is_modified());
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  QCOMPARE(f.readAll(), QByteArray("# B\n"));
}

void TestFileWatchWindow::save_write_failure_keeps_tab() {
  if (!file_modes_apply()) QSKIP("file modes do not apply (root)");
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString notes = d.filePath(QStringLiteral("notes"));
  QVERIFY(d.mkdir(QStringLiteral("notes")));
  const QString path = write_file(QDir(notes), QStringLiteral("a.md"), QByteArray("# A\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  tab->editor()->insert_text("x");

  rewrite_in_place(path, QByteArray("# B\n"));
  QVERIFY(QFile::setPermissions(notes, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
  answer_file_dialog(QDir(notes).filePath(QStringLiteral("b.md")));
  auto step = std::make_shared<int>(0);
  auto* timer = new QTimer();
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [timer, step] {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (box == nullptr) return;
    if (*step == 0) {
      QAbstractButton* button = box->button(QMessageBox::Save);
      if (button != nullptr) button->click();
      *step = 1;
    } else {
      QAbstractButton* button = box->button(QMessageBox::Ok);
      if (button != nullptr) button->click();
      *step = 2;
      timer->stop();
      timer->deleteLater();
    }
  });
  timer->start();
  const bool closed = window.close_tab(0);
  QTest::qWait(150);
  QVERIFY(QFile::setPermissions(notes, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

  QCOMPARE(*step, 2);
  QVERIFY(!closed);
  QCOMPARE(tab_widget(window)->count(), 1);
  QCOMPARE(tab->editor()->text(), std::string("x# A\n"));
  QVERIFY(tab->editor()->is_modified());
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  QCOMPARE(f.readAll(), QByteArray("# B\n"));
  QVERIFY(!QFile::exists(QDir(notes).filePath(QStringLiteral("b.md"))));
}

void TestFileWatchWindow::watch_too_many_disables_show_diff() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QByteArray base = distinct_lines(QStringLiteral("base"), 1100);
  const QString path = write_file(d, QStringLiteral("a.md"), base);
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  tab->editor()->insert_text("x");

  rewrite_in_place(path, distinct_lines(QStringLiteral("other"), 1100));
  bool enabled = true;
  check_button_enabled_then_click(QStringLiteral("Show diff"), &enabled, QStringLiteral("Cancel"));
  window.check_files();
  QTest::qWait(150);

  QVERIFY(!enabled);
}

void TestFileWatchWindow::base_follows_saves_no_conflict_after_second_save() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("1\n2\n3\n4\n5\n6\n7\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();

  tab->editor()->replace_text("1\n2\n3\n4\n5\n6\nSAVED7\n");
  trigger_save(window);
  QTest::qWait(50);
  QVERIFY(!tab->editor()->is_modified());
  QCOMPARE(tab->base(), QByteArray("1\n2\n3\n4\n5\n6\nSAVED7\n"));

  tab->editor()->replace_text("1\n2\n3\n4\n5\n6\nY7\n");
  rewrite_in_place(path, QByteArray("O1\n2\n3\n4\n5\n6\nSAVED7\n"));

  auto conflicts = std::make_shared<int>(-1);
  auto dialogs = std::make_shared<int>(0);
  auto* timer = new QTimer();
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [timer, dialogs] {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (box == nullptr) return;
    ++*dialogs;
    const QString button_text = (*dialogs == 1) ? QStringLiteral("Show diff") : QStringLiteral("Cancel");
    for (QAbstractButton* button : box->buttons()) {
      if (button->text() == button_text) {
        button->click();
        break;
      }
    }
    if (*dialogs == 2) {
      timer->stop();
      timer->deleteLater();
    }
  });
  timer->start();
  handle_merge_window(std::nullopt, conflicts);
  window.check_files();
  QTest::qWait(150);

  QCOMPARE(*conflicts, 0);
  QCOMPARE(*dialogs, 2);
}

void TestFileWatchWindow::disk_merge_use_log_has_no_document_text() {
  constexpr std::string_view marker = "QZXMERGEWINMARK";
  std::vector<std::string> lines;
  lumos::set_log_sink([&](lumos::LogLevel, std::string_view line) { lines.emplace_back(line); });
  lumos::set_log_level(lumos::LogLevel::debug);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString path = write_file(d, QStringLiteral("a.md"), QByteArray("1\n2\n3\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  make_active(window);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  const QString marker_text = QString::fromLatin1(marker.data(), static_cast<int>(marker.size()));
  tab->editor()->replace_text((marker_text + QStringLiteral("\n2\n3\n")).toStdString());

  rewrite_in_place(path, QByteArray("1\n2\nO3\n"));
  click_dialog_text(QStringLiteral("Show diff"));
  handle_merge_window(std::vector<Pick>{});
  window.check_files();
  QTest::qWait(150);

  lumos::set_log_sink(nullptr);
  lumos::set_log_level(lumos::LogLevel::info);

  int merge_use_lines = 0;
  for (const std::string& line : lines) {
    if (line.find("event=disk_merge_use") == std::string::npos) continue;
    ++merge_use_lines;
    QVERIFY2(line.find(marker) == std::string::npos, line.c_str());
  }
  QVERIFY(merge_use_lines > 0);
}

QTEST_MAIN(TestFileWatchWindow)
#include "test_file_watch_window.moc"
