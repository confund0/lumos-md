
#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QList>
#include <QMessageBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "dialog_util.hpp"
#include "document_tab.hpp"
#include "drafts.hpp"
#include "lumos/log.hpp"
#include "lumos/qt/source_editor.hpp"
#include "main_window.hpp"
#include "open_tabs.hpp"

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

QString draft_file(const QString& drafts_dir, const QString& id) { return QDir(drafts_dir).filePath(id + QStringLiteral(".draft")); }

QString write_path_draft(const QString& drafts_dir, const QString& id, const QString& path, const QByteArray& saved_sha256,
                          const QByteArray& text) {
  const Draft draft{.untitled = false, .path = path, .saved_sha256 = saved_sha256, .text = text};
  const bool ok = write_draft(drafts_dir, id, draft);
  Q_ASSERT(ok);
  Q_UNUSED(ok);
  return id;
}

QString write_damaged_path_draft(const QString& drafts_dir, const QString& path, const QByteArray& saved_sha256,
                                  const QByteArray& full_text, qsizetype keep_bytes) {
  const QString id = path_draft_id(path);
  const Draft draft{.untitled = false, .path = path, .saved_sha256 = saved_sha256, .text = full_text};
  QByteArray bytes = encode_draft(draft);
  bytes.truncate(bytes.size() - (full_text.size() - keep_bytes));
  const bool made_dir = QDir().mkpath(drafts_dir);
  Q_ASSERT(made_dir);
  Q_UNUSED(made_dir);
  QFile f(draft_file(drafts_dir, id));
  const bool opened = f.open(QIODevice::WriteOnly);
  Q_ASSERT(opened);
  Q_UNUSED(opened);
  f.write(bytes);
  f.close();
  return id;
}

QString write_unreadable_path_draft(const QString& drafts_dir, const QString& path, const QByteArray& bytes) {
  const QString id = path_draft_id(path);
  const bool made_dir = QDir().mkpath(drafts_dir);
  Q_ASSERT(made_dir);
  Q_UNUSED(made_dir);
  QFile f(draft_file(drafts_dir, id));
  const bool opened = f.open(QIODevice::WriteOnly);
  Q_ASSERT(opened);
  Q_UNUSED(opened);
  f.write(bytes);
  f.close();
  return id;
}

}  

class TestDraftsWindow : public QObject {
  Q_OBJECT

 private slots:
  void first_edit_delays_write();
  void later_edit_does_not_push_back();
  void save_deletes_draft();
  void undo_to_saved_deletes_draft();
  void revert_deletes_draft();
  void close_tab_discard_deletes_cancel_keeps();
  void window_close_dialogs_and_drafts();
  void close_tab_discard_then_wait_writes_nothing();
  void save_as_of_untitled_deletes_draft();
  void tab_list_gets_draft_entries();
  void restore_tabs_opens_path_draft_path();
  void restore_opens_unnamed_draft();
  void restore_opens_unreadable_draft_as_bytes();
  void restore_skips_locked_draft();
  void empty_drafts_dir_writes_nothing();
  void draft_logs_have_no_marker();
  void recover_adopts_draft_undo_removes_it();
  void discard_keeps_file_text_removes_draft();
  void escape_on_dialog_means_recover();
  void damaged_draft_informative_and_partial_recover();
  void unreadable_draft_informative_and_full_bytes_recover();
  void changed_on_disk_shows_changed_line();
  void equal_draft_removed_without_dialog();
  void locked_draft_skipped_second_id_on_write();
  void only_newest_path_draft_offered();
  void restore_tabs_opens_unnamed_path_draft();
  void restore_tabs_opens_gone_path_draft_as_untitled();
  void restore_tabs_skips_older_draft_of_open_path();
  void reopen_closed_tab_shows_recover_dialog();
};

void TestDraftsWindow::first_edit_delays_write() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  DocumentTab* tab = window.current_tab();

  tab->editor()->insert_text("x");
  QTest::qWait(2500);
  QVERIFY(tab->draft_id().isEmpty());
  QVERIFY(QTest::qWaitFor([&] { return !tab->draft_id().isEmpty(); }, 2500));
  QVERIFY(QFile::exists(draft_file(drafts_dir, tab->draft_id())));
}

void TestDraftsWindow::later_edit_does_not_push_back() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  DocumentTab* tab = window.current_tab();

  tab->editor()->insert_text("x");
  QTest::qWait(2000);
  tab->editor()->insert_text("y");
  QVERIFY(QTest::qWaitFor([&] { return !tab->draft_id().isEmpty(); }, 1500));
}

void TestDraftsWindow::save_deletes_draft() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  const QString path = write_file(d, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();

  tab->editor()->insert_text("x");
  tab->write_draft_now();
  const QString id = tab->draft_id();
  QVERIFY(!id.isEmpty());
  QVERIFY(QFile::exists(draft_file(drafts_dir, id)));

  QString error;
  QVERIFY(tab->write(path, &error));
  QVERIFY(tab->draft_id().isEmpty());
  QVERIFY(!QFile::exists(draft_file(drafts_dir, id)));
}

void TestDraftsWindow::undo_to_saved_deletes_draft() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  DocumentTab* tab = window.current_tab();

  tab->editor()->insert_text("x");
  tab->write_draft_now();
  const QString id = tab->draft_id();
  QVERIFY(!id.isEmpty());

  tab->editor()->undo();
  QVERIFY(tab->draft_id().isEmpty());
  QVERIFY(!QFile::exists(draft_file(drafts_dir, id)));
}

void TestDraftsWindow::revert_deletes_draft() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  const QString path = write_file(d, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();

  tab->editor()->insert_text("x");
  tab->write_draft_now();
  const QString id = tab->draft_id();
  QVERIFY(!id.isEmpty());

  QString error;
  QVERIFY(tab->load(path, true, &error));
  QVERIFY(tab->draft_id().isEmpty());
  QVERIFY(!QFile::exists(draft_file(drafts_dir, id)));
}

void TestDraftsWindow::close_tab_discard_deletes_cancel_keeps() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  DocumentTab* tab = window.current_tab();

  tab->editor()->insert_text("x");
  tab->write_draft_now();
  const QString id = tab->draft_id();
  QVERIFY(!id.isEmpty());

  click_dialog(QMessageBox::Cancel);
  QVERIFY(!window.close_tab(0));
  QCOMPARE(window.tab_count(), 1);
  QVERIFY(QFile::exists(draft_file(drafts_dir, id)));

  click_dialog(QMessageBox::Discard);
  QVERIFY(window.close_tab(0));
  QVERIFY(!QFile::exists(draft_file(drafts_dir, id)));
}

void TestDraftsWindow::window_close_dialogs_and_drafts() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString tabs_file = d.filePath(QStringLiteral("open-tabs"));
  auto window = std::make_unique<MainWindow>(d.filePath(QStringLiteral("settings.ini")), tabs_file, drafts_dir);

  DocumentTab* tab1 = window->current_tab();
  tab1->editor()->insert_text("x");
  tab1->write_draft_now();
  const QString id1 = tab1->draft_id();
  QVERIFY(!id1.isEmpty());

  window->new_tab();
  DocumentTab* tab2 = window->current_tab();
  tab2->editor()->insert_text("y");
  tab2->write_draft_now();
  const QString id2 = tab2->draft_id();
  QVERIFY(!id2.isEmpty());

  const QString file1 = draft_file(drafts_dir, id1);
  const QString file2 = draft_file(drafts_dir, id2);

  click_dialogs({QMessageBox::Discard, QMessageBox::Cancel});
  window->close();
  QVERIFY(QFile::exists(file1));
  QVERIFY(QFile::exists(file2));

  click_dialogs({QMessageBox::Discard, QMessageBox::Discard});
  window->close();
  QVERIFY(!QFile::exists(file1));
  QVERIFY(!QFile::exists(file2));
  QVERIFY(read_open_tabs(tabs_file).paths.isEmpty());
}

void TestDraftsWindow::close_tab_discard_then_wait_writes_nothing() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  DocumentTab* tab = window.current_tab();

  tab->editor()->insert_text("x");
  tab->write_draft_now();
  const QString id = tab->draft_id();
  QVERIFY(!id.isEmpty());

  click_dialog(QMessageBox::Discard);
  QVERIFY(window.close_tab(0));
  QVERIFY(!QFile::exists(draft_file(drafts_dir, id)));

  QTest::qWait(3500);
  QVERIFY(!QFile::exists(draft_file(drafts_dir, id)));
  QVERIFY(list_draft_ids(drafts_dir).isEmpty());
}

void TestDraftsWindow::save_as_of_untitled_deletes_draft() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString tabs_file = d.filePath(QStringLiteral("open-tabs"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")), tabs_file, drafts_dir);
  DocumentTab* tab = window.current_tab();

  tab->editor()->insert_text("x");
  tab->write_draft_now();
  const QString id = tab->draft_id();
  QVERIFY(!id.isEmpty());
  QVERIFY(QTest::qWaitFor([&] { return read_open_tabs(tabs_file).paths == QStringList{QStringLiteral("draft:") + id}; }, 2000));

  const QString path = d.filePath(QStringLiteral("saved.md"));
  QString error;
  QVERIFY(tab->write(path, &error));
  tab->set_path(path);

  QVERIFY(tab->draft_id().isEmpty());
  QVERIFY(!QFile::exists(draft_file(drafts_dir, id)));
  QVERIFY(QTest::qWaitFor([&] { return read_open_tabs(tabs_file).paths == QStringList{path}; }, 2000));
}

void TestDraftsWindow::tab_list_gets_draft_entries() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString tabs_file = d.filePath(QStringLiteral("open-tabs"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")), tabs_file, drafts_dir);

  DocumentTab* tab1 = window.current_tab();
  tab1->editor()->insert_text("a");
  tab1->write_draft_now();
  const QString id1 = tab1->draft_id();
  QVERIFY(!id1.isEmpty());

  window.new_tab();
  DocumentTab* tab2 = window.current_tab();
  tab2->editor()->insert_text("b");
  tab2->write_draft_now();
  const QString id2 = tab2->draft_id();
  QVERIFY(!id2.isEmpty());

  QVERIFY(QTest::qWaitFor([&] { return read_open_tabs(tabs_file).paths.size() == 2; }, 2000));
  const OpenTabsRead read = read_open_tabs(tabs_file);
  QCOMPARE(read.paths, (QStringList{QStringLiteral("draft:") + id1, QStringLiteral("draft:") + id2}));
  QCOMPARE(read.active, QStringLiteral("draft:") + id2);
}

void TestDraftsWindow::restore_tabs_opens_path_draft_path() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString a = write_file(d, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QString b = write_file(d, QStringLiteral("b.md"), QStringLiteral("# B\n"));

  const QString id = new_untitled_draft_id();
  const Draft draft{.untitled = true, .path = "", .saved_sha256 = "", .text = QByteArray("draft text")};
  QVERIFY(write_draft(drafts_dir, id, draft));

  const QString tabs_file = d.filePath(QStringLiteral("open-tabs"));
  QVERIFY(write_open_tabs(tabs_file, {a, QStringLiteral("draft:") + id, b}, QString()));

  MainWindow window(d.filePath(QStringLiteral("settings.ini")), tabs_file, drafts_dir);
  window.restore_tabs();
  QCOMPARE(window.tab_count(), 3);
  QCOMPARE(window.tab_at(0)->path(), a);
  QCOMPARE(window.tab_at(1)->draft_id(), id);
  QVERIFY(window.tab_at(1)->editor()->is_modified());
  QCOMPARE(window.tab_at(1)->editor()->text(), std::string("draft text"));
  QCOMPARE(window.tab_at(2)->path(), b);

  window.tab_at(1)->editor()->undo();
  QCOMPARE(window.tab_at(1)->editor()->text(), std::string());
}

void TestDraftsWindow::restore_opens_unnamed_draft() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString id = new_untitled_draft_id();
  const Draft draft{.untitled = true, .path = "", .saved_sha256 = "", .text = QByteArray("orphan text")};
  QVERIFY(write_draft(drafts_dir, id, draft));

  MainWindow window(d.filePath(QStringLiteral("settings.ini")), d.filePath(QStringLiteral("open-tabs")), drafts_dir);
  window.restore_tabs();

  bool found = false;
  for (int i = 0; i < window.tab_count(); ++i) {
    if (window.tab_at(i)->draft_id() == id) {
      found = true;
      QCOMPARE(window.tab_at(i)->editor()->text(), std::string("orphan text"));
    }
  }
  QVERIFY(found);
}

void TestDraftsWindow::restore_opens_unreadable_draft_as_bytes() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  QVERIFY(QDir().mkpath(drafts_dir));
  const QString id = QStringLiteral("u-badbadbadbadbad");
  const QString file = draft_file(drafts_dir, id);
  const QByteArray bytes = "not a draft header, no blank line here";
  QFile f(file);
  QVERIFY(f.open(QIODevice::WriteOnly));
  QCOMPARE(f.write(bytes), static_cast<qint64>(bytes.size()));
  f.close();

  MainWindow window(d.filePath(QStringLiteral("settings.ini")), d.filePath(QStringLiteral("open-tabs")), drafts_dir);
  window.restore_tabs();

  bool found = false;
  for (int i = 0; i < window.tab_count(); ++i) {
    if (window.tab_at(i)->draft_id() == id) {
      found = true;
      QCOMPARE(window.tab_at(i)->editor()->text(), std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    }
  }
  QVERIFY(found);
  QVERIFY(QFile::exists(file));
}

void TestDraftsWindow::restore_skips_locked_draft() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString id = new_untitled_draft_id();
  const Draft draft{.untitled = true, .path = "", .saved_sha256 = "", .text = QByteArray("locked text")};
  QVERIFY(write_draft(drafts_dir, id, draft));
  std::unique_ptr<QLockFile> lock = lock_draft(drafts_dir, id);
  QVERIFY(lock != nullptr);

  MainWindow window(d.filePath(QStringLiteral("settings.ini")), d.filePath(QStringLiteral("open-tabs")), drafts_dir);
  window.restore_tabs();

  for (int i = 0; i < window.tab_count(); ++i) QVERIFY(window.tab_at(i)->draft_id() != id);
  QVERIFY(QFile::exists(draft_file(drafts_dir, id)));
}

void TestDraftsWindow::empty_drafts_dir_writes_nothing() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  MainWindow window(d.filePath(QStringLiteral("settings.ini")));
  DocumentTab* tab = window.current_tab();

  tab->editor()->insert_text("x");
  tab->write_draft_now();
  QVERIFY(tab->draft_id().isEmpty());

  QTest::qWait(3500);
  QVERIFY(tab->draft_id().isEmpty());
}

void TestDraftsWindow::draft_logs_have_no_marker() {
  constexpr std::string_view marker = "QZXDRAFTWINMARK";
  std::vector<std::string> lines;
  lumos::set_log_sink([&](lumos::LogLevel, std::string_view line) { lines.emplace_back(line); });
  lumos::set_log_level(lumos::LogLevel::debug);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString name = QStringLiteral("%1.md").arg(QString::fromLatin1(marker.data(), static_cast<int>(marker.size())));
  const QString path = write_file(d, name, QStringLiteral("x\n"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();
  tab->editor()->insert_text(marker);
  tab->write_draft_now();
  QVERIFY(!tab->draft_id().isEmpty());
  QString error;
  QVERIFY(tab->write(path, &error));  

  lumos::set_log_sink(nullptr);
  lumos::set_log_level(lumos::LogLevel::info);

  int draft_lines = 0;
  for (const std::string& line : lines) {
    if (line.find("event=draft") == std::string::npos) continue;
    ++draft_lines;
    QVERIFY2(line.find(marker) == std::string::npos, line.c_str());
  }
  QVERIFY(draft_lines > 0);
}

void TestDraftsWindow::recover_adopts_draft_undo_removes_it() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString path = write_file(d, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QByteArray sha = sha256_hex(QByteArray("# A\n"));
  const QString id = write_path_draft(drafts_dir, path_draft_id(path), path, sha, QByteArray("# A\ndraft text"));

  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  click_dialog_text(QStringLiteral("Recover"));
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();

  QCOMPARE(tab->editor()->text(), std::string("# A\ndraft text"));
  QVERIFY(tab->editor()->is_modified());
  QVERIFY(QFile::exists(draft_file(drafts_dir, id)));

  tab->editor()->undo();
  QCOMPARE(tab->editor()->text(), std::string("# A\n"));
  QVERIFY(!tab->editor()->is_modified());
  QVERIFY(QTest::qWaitFor([&] { return !QFile::exists(draft_file(drafts_dir, id)); }, 2000));
}

void TestDraftsWindow::discard_keeps_file_text_removes_draft() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString path = write_file(d, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QByteArray sha = sha256_hex(QByteArray("# A\n"));
  const QString id = write_path_draft(drafts_dir, path_draft_id(path), path, sha, QByteArray("# A\ndraft text"));

  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  click_dialog_text(QStringLiteral("Discard"));
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();

  QCOMPARE(tab->editor()->text(), std::string("# A\n"));
  QVERIFY(!tab->editor()->is_modified());
  QVERIFY(!QFile::exists(draft_file(drafts_dir, id)));
}

void TestDraftsWindow::escape_on_dialog_means_recover() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString path = write_file(d, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QByteArray sha = sha256_hex(QByteArray("# A\n"));
  write_path_draft(drafts_dir, path_draft_id(path), path, sha, QByteArray("# A\ndraft text"));

  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  escape_dialog();
  QVERIFY(window.open_file(path));
  DocumentTab* tab = window.current_tab();

  QCOMPARE(tab->editor()->text(), std::string("# A\ndraft text"));
  QVERIFY(tab->editor()->is_modified());
}

void TestDraftsWindow::damaged_draft_informative_and_partial_recover() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString path = write_file(d, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QByteArray sha = sha256_hex(QByteArray("# A\n"));
  const QByteArray full_text = "# A\nfull draft text";
  const QString id = write_damaged_path_draft(drafts_dir, path, sha, full_text, 10);

  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  QString informative;
  click_dialog_text(QStringLiteral("Recover"), nullptr, &informative);
  QVERIFY(window.open_file(path));

  QVERIFY2(informative.contains(QStringLiteral("damaged")), informative.toUtf8().constData());
  DocumentTab* tab = window.current_tab();
  QCOMPARE(tab->editor()->text(), std::string(full_text.left(10).constData(), 10));
  QVERIFY(QFile::exists(draft_file(drafts_dir, id)));
}

void TestDraftsWindow::unreadable_draft_informative_and_full_bytes_recover() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString path = write_file(d, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QByteArray bytes = "not a draft header, no blank line here";
  write_unreadable_path_draft(drafts_dir, path, bytes);

  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  QString informative;
  click_dialog_text(QStringLiteral("Recover"), nullptr, &informative);
  QVERIFY(window.open_file(path));

  QVERIFY2(informative.contains(QStringLiteral("damaged")), informative.toUtf8().constData());
  DocumentTab* tab = window.current_tab();
  QCOMPARE(tab->editor()->text(), std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
}

void TestDraftsWindow::changed_on_disk_shows_changed_line() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString path = write_file(d, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QByteArray stale_sha = sha256_hex(QByteArray("an older file version"));
  write_path_draft(drafts_dir, path_draft_id(path), path, stale_sha, QByteArray("# A\ndraft text"));

  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  QString informative;
  click_dialog_text(QStringLiteral("Discard"), nullptr, &informative);
  QVERIFY(window.open_file(path));

  QVERIFY2(informative.contains(QStringLiteral("changed")), informative.toUtf8().constData());
}

void TestDraftsWindow::equal_draft_removed_without_dialog() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString path = write_file(d, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QByteArray sha = sha256_hex(QByteArray("# A\n"));
  const QString id = write_path_draft(drafts_dir, path_draft_id(path), path, sha, QByteArray("# A\n"));

  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  QVERIFY(window.open_file(path));

  QVERIFY(!QFile::exists(draft_file(drafts_dir, id)));
  DocumentTab* tab = window.current_tab();
  QVERIFY(!tab->editor()->is_modified());
}

void TestDraftsWindow::locked_draft_skipped_second_id_on_write() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString path = write_file(d, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QByteArray sha = sha256_hex(QByteArray("# A\n"));
  const QString id = write_path_draft(drafts_dir, path_draft_id(path), path, sha, QByteArray("# A\nlocked draft"));
  std::unique_ptr<QLockFile> lock = lock_draft(drafts_dir, id);
  QVERIFY(lock != nullptr);

  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  QVERIFY(window.open_file(path));
  QVERIFY(QFile::exists(draft_file(drafts_dir, id)));

  DocumentTab* tab = window.current_tab();
  tab->editor()->insert_text("x");
  tab->write_draft_now();
  QCOMPARE(tab->draft_id(), id + QStringLiteral("-2"));
}

void TestDraftsWindow::only_newest_path_draft_offered() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString path = write_file(d, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QByteArray sha = sha256_hex(QByteArray("# A\n"));
  const QString id1 = write_path_draft(drafts_dir, path_draft_id(path), path, sha, QByteArray("# A\nolder draft"));
  QTest::qWait(20);  
  const QString id2 = write_path_draft(drafts_dir, id1 + QStringLiteral("-2"), path, sha, QByteArray("# A\nnewer draft"));

  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  QString text;
  click_dialog_text(QStringLiteral("Discard"), &text, nullptr);
  QVERIFY(window.open_file(path));

  QVERIFY(!QFile::exists(draft_file(drafts_dir, id2)));
  QVERIFY(QFile::exists(draft_file(drafts_dir, id1)));
}

void TestDraftsWindow::restore_tabs_opens_unnamed_path_draft() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString path = write_file(d, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QByteArray sha = sha256_hex(QByteArray("# A\n"));
  write_path_draft(drafts_dir, path_draft_id(path), path, sha, QByteArray("# A\ndraft text"));

  const QString tabs_file = d.filePath(QStringLiteral("open-tabs"));  
  MainWindow window(d.filePath(QStringLiteral("settings.ini")), tabs_file, drafts_dir);
  click_dialog_text(QStringLiteral("Recover"));
  window.restore_tabs();

  bool found = false;
  for (int i = 0; i < window.tab_count(); ++i) {
    if (window.tab_at(i)->path() == path) {
      found = true;
      QCOMPARE(window.tab_at(i)->editor()->text(), std::string("# A\ndraft text"));
    }
  }
  QVERIFY(found);
}

void TestDraftsWindow::restore_tabs_opens_gone_path_draft_as_untitled() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString path = d.filePath(QStringLiteral("gone.md"));  
  const QByteArray sha = sha256_hex(QByteArray());
  const QString id = write_path_draft(drafts_dir, path_draft_id(path), path, sha, QByteArray("gone draft text"));

  const QString tabs_file = d.filePath(QStringLiteral("open-tabs"));
  MainWindow window(d.filePath(QStringLiteral("settings.ini")), tabs_file, drafts_dir);
  window.restore_tabs();

  bool found = false;
  for (int i = 0; i < window.tab_count(); ++i) {
    if (window.tab_at(i)->draft_id() == id) {
      found = true;
      QCOMPARE(window.tab_at(i)->editor()->text(), std::string("gone draft text"));
    }
  }
  QVERIFY(found);
  QVERIFY(QFile::exists(draft_file(drafts_dir, id)));
  auto* notice = window.findChild<QLabel*>(QStringLiteral("notice"));
  QVERIFY(notice != nullptr);
  QCOMPARE(notice->text(), QObject::tr("ℹ The file of a draft is gone; its text opened as an Untitled tab."));
}

void TestDraftsWindow::restore_tabs_skips_older_draft_of_open_path() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString path = write_file(d, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QByteArray sha = sha256_hex(QByteArray("# A\n"));
  const QString id1 = write_path_draft(drafts_dir, path_draft_id(path), path, sha, QByteArray("# A\nolder draft"));
  QTest::qWait(20);  
  const QString id2 = write_path_draft(drafts_dir, id1 + QStringLiteral("-2"), path, sha, QByteArray("# A\nnewer draft"));

  const QString tabs_file = d.filePath(QStringLiteral("open-tabs"));
  QVERIFY(write_open_tabs(tabs_file, {path}, path));

  MainWindow window(d.filePath(QStringLiteral("settings.ini")), tabs_file, drafts_dir);
  click_dialog_text(QStringLiteral("Discard"));
  window.restore_tabs();

  QVERIFY(!QFile::exists(draft_file(drafts_dir, id2)));
  QVERIFY(QFile::exists(draft_file(drafts_dir, id1)));
}

void TestDraftsWindow::reopen_closed_tab_shows_recover_dialog() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir d(dir.path());
  const QString drafts_dir = d.filePath(QStringLiteral("drafts"));
  const QString path = write_file(d, QStringLiteral("a.md"), QStringLiteral("# A\n"));

  MainWindow window(d.filePath(QStringLiteral("settings.ini")), QString(), drafts_dir);
  QVERIFY(window.open_file(path));
  QVERIFY(window.close_tab(0));  

  const QByteArray sha = sha256_hex(QByteArray("# A\n"));
  write_path_draft(drafts_dir, path_draft_id(path), path, sha, QByteArray("# A\ndraft text"));

  click_dialog_text(QStringLiteral("Recover"));
  window.reopen_closed_tab();

  DocumentTab* tab = window.current_tab();
  QCOMPARE(tab->path(), path);
  QCOMPARE(tab->editor()->text(), std::string("# A\ndraft text"));
}

QTEST_MAIN(TestDraftsWindow)
#include "test_drafts_window.moc"
