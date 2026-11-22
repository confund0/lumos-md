
#include <QAction>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>
#include <future>
#include <optional>
#include <thread>

#include "dialog_util.hpp"
#include "document_tab.hpp"
#include "mcp_server.hpp"
#include "main_window.hpp"
#include "mcp_bridge.hpp"
#include "single_instance.hpp"
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

template <typename T>
T wait_for(std::future<T>& future) {
  while (future.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready) QCoreApplication::processEvents();
  return future.get();
}

std::optional<QJsonObject> ask(const QString& name, const QJsonObject& request, int connect_ms = 2000,
                                int answer_ms = 5000) {
  std::packaged_task<std::optional<QJsonObject>()> task(
      [&] { return ask_window(name, request, connect_ms, answer_ms); });
  auto future = task.get_future();
  std::thread thread(std::move(task));
  const auto result = wait_for(future);
  thread.join();
  return result;
}

std::optional<QByteArray> raw_request(const QString& name, const QByteArray& payload, int timeout_ms = 2000) {
  std::packaged_task<std::optional<QByteArray>()> task([&] {
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(timeout_ms)) return std::optional<QByteArray>();
    socket.write(payload);
    while (socket.bytesToWrite() > 0) {
      if (!socket.waitForBytesWritten(timeout_ms)) break;
    }
    QByteArray reply;
    while (!reply.contains('\n')) {
      if (!socket.waitForReadyRead(timeout_ms)) break;
      reply += socket.readAll();
    }
    return std::optional<QByteArray>(reply);
  });
  auto future = task.get_future();
  std::thread thread(std::move(task));
  const auto result = wait_for(future);
  thread.join();
  return result;
}

QAction* mcp_action(MainWindow& window) { return window.findChild<QAction*>(QStringLiteral("mcp_enabled")); }

QTabWidget* tab_widget(MainWindow& window) { return window.tabs_for_tests(); }

QAction* named_action(MainWindow& window, const char* name) {
  return window.findChild<QAction*>(QString::fromLatin1(name));
}

void choose_socket_dir(MainWindow& window, const QString& dir, QString* command) {
  answer_file_dialog(dir);
  auto* timer = new QTimer();
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [timer, command] {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (box == nullptr) return;
    *command = box->informativeText();
    box->button(QMessageBox::Ok)->click();
    timer->stop();
    timer->deleteLater();
  });
  timer->start();
  named_action(window, "mcp_socket_choose")->trigger();
}

}  

class TestMcpServer : public QObject {
  Q_OBJECT

 private slots:
  void switch_off_by_default();
  void switch_on_listens_and_persists();
  void list_gives_files_in_tab_order();
  void read_gives_tab_text();
  void read_non_utf8_gives_utf8_false();
  void bad_requests();
  void read_large_text_exact();
  void switch_off_again_stops_listening();
  void end_to_end_through_mcp_bridge();
  void mcp_extra_arg_exits_with_error();
  void socket_folder_pick_and_default();
  void socket_folder_never_removes_a_file();
  void listen_replaces_a_stale_socket();
  void end_to_end_with_mcp_dir();
};

void TestMcpServer::switch_off_by_default() {
  QTemporaryDir settings_dir;
  QTemporaryDir socket_dir;
  QVERIFY(settings_dir.isValid() && socket_dir.isValid());
  MainWindow window(QDir(settings_dir.path()).filePath(QStringLiteral("settings.ini")));
  const QString name = QDir(socket_dir.path()).filePath(QStringLiteral("mcp"));
  window.set_mcp_server_name(name);

  QVERIFY(!window.mcp_listening());
  QVERIFY(!ask(name, QJsonObject{{"v", 1}, {"op", "list"}}, 300, 300).has_value());
}

void TestMcpServer::switch_on_listens_and_persists() {
  QTemporaryDir settings_dir;
  QTemporaryDir socket_dir;
  const QString settings_file = QDir(settings_dir.path()).filePath(QStringLiteral("settings.ini"));
  const QString name = QDir(socket_dir.path()).filePath(QStringLiteral("mcp"));

  MainWindow window(settings_file);
  window.set_mcp_server_name(name);
  QAction* action = mcp_action(window);
  QVERIFY(action != nullptr);
  action->setChecked(true);
  QVERIFY(window.mcp_listening());
  QVERIFY(ask(name, QJsonObject{{"v", 1}, {"op", "list"}}).has_value());

  QTest::qWait(50);
  QSettings settings(settings_file, QSettings::IniFormat);
  QCOMPARE(settings.value(QStringLiteral("mcp/enabled")).toBool(), true);

  MainWindow second(settings_file);
  QAction* second_action = mcp_action(second);
  QVERIFY(second_action != nullptr);
  QVERIFY(second_action->isChecked());
}

void TestMcpServer::list_gives_files_in_tab_order() {
  QTemporaryDir settings_dir;
  QTemporaryDir work_dir;
  QTemporaryDir socket_dir;
  const QDir dir(work_dir.path());
  const QString a = write_file(dir, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QString b = write_file(dir, QStringLiteral("b.md"), QStringLiteral("# B\n"));

  MainWindow window(QDir(settings_dir.path()).filePath(QStringLiteral("settings.ini")));
  const QString name = QDir(socket_dir.path()).filePath(QStringLiteral("mcp"));
  window.set_mcp_server_name(name);
  mcp_action(window)->setChecked(true);

  QVERIFY(window.open_file(a));
  QVERIFY(window.open_file(b));
  window.new_tab();
  tab_widget(window)->setCurrentIndex(1);
  window.tab_at(0)->editor()->insert_text("x");

  const auto answer = ask(name, QJsonObject{{"v", 1}, {"op", "list"}});
  QVERIFY(answer.has_value());
  const QJsonObject expected{
      {"files", QJsonArray{QJsonObject{{"path", a}, {"name", QStringLiteral("a.md")}, {"modified", true},
                                        {"current", false}},
                            QJsonObject{{"path", b}, {"name", QStringLiteral("b.md")}, {"modified", false},
                                        {"current", true}},
                            QJsonObject{{"path", QJsonValue()}, {"name", QStringLiteral("Untitled")},
                                        {"modified", false}, {"current", false}}}}};
  QCOMPARE(*answer, expected);
}

void TestMcpServer::read_gives_tab_text() {
  QTemporaryDir settings_dir;
  QTemporaryDir work_dir;
  QTemporaryDir socket_dir;
  QDir dir(work_dir.path());
  QVERIFY(dir.mkdir(QStringLiteral("sub")));
  const QString a = write_file(dir, QStringLiteral("a.md"), QString::fromUtf8("\xc3\xa9\n"));  
  const QString not_open = write_file(dir, QStringLiteral("b.md"), QStringLiteral("# B\n"));

  MainWindow window(QDir(settings_dir.path()).filePath(QStringLiteral("settings.ini")));
  const QString name = QDir(socket_dir.path()).filePath(QStringLiteral("mcp"));
  window.set_mcp_server_name(name);
  mcp_action(window)->setChecked(true);

  QVERIFY(window.open_file(a));
  window.current_tab()->editor()->insert_text("x");
  const QString expected_text = QString::fromUtf8(QByteArray::fromStdString(window.current_tab()->editor()->text()));

  const auto direct = ask(name, QJsonObject{{"v", 1}, {"op", "read"}, {"path", a}});
  QVERIFY(direct.has_value());
  QCOMPARE(direct->value("open").toBool(), true);
  QCOMPARE(direct->value("text").toString(), expected_text);

  const QString via_dotdot = dir.filePath(QStringLiteral("sub/../a.md"));
  const auto via_dotdot_answer = ask(name, QJsonObject{{"v", 1}, {"op", "read"}, {"path", via_dotdot}});
  QVERIFY(via_dotdot_answer.has_value());
  QCOMPARE(via_dotdot_answer->value("text").toString(), expected_text);

  const auto not_open_answer = ask(name, QJsonObject{{"v", 1}, {"op", "read"}, {"path", not_open}});
  QVERIFY(not_open_answer.has_value());
  QCOMPARE(not_open_answer->value("open").toBool(), false);

  const QString missing = dir.filePath(QStringLiteral("missing.md"));
  const auto missing_answer = ask(name, QJsonObject{{"v", 1}, {"op", "read"}, {"path", missing}});
  QVERIFY(missing_answer.has_value());
  QCOMPARE(missing_answer->value("open").toBool(), false);
}

void TestMcpServer::read_non_utf8_gives_utf8_false() {
  QTemporaryDir settings_dir;
  QTemporaryDir work_dir;
  QTemporaryDir socket_dir;
  QDir dir(work_dir.path());
  const QString bad = write_bytes(dir, QStringLiteral("bad.md"), QByteArray("\xff\xfe"));

  MainWindow window(QDir(settings_dir.path()).filePath(QStringLiteral("settings.ini")));
  const QString name = QDir(socket_dir.path()).filePath(QStringLiteral("mcp"));
  window.set_mcp_server_name(name);
  mcp_action(window)->setChecked(true);
  QVERIFY(window.open_file(bad));

  const auto answer = ask(name, QJsonObject{{"v", 1}, {"op", "read"}, {"path", bad}});
  QVERIFY(answer.has_value());
  QCOMPARE(answer->value("open").toBool(), true);
  QCOMPARE(answer->value("utf8").toBool(), false);
  QVERIFY(!answer->contains("text"));
}

void TestMcpServer::bad_requests() {
  QTemporaryDir settings_dir;
  QTemporaryDir socket_dir;
  MainWindow window(QDir(settings_dir.path()).filePath(QStringLiteral("settings.ini")));
  const QString name = QDir(socket_dir.path()).filePath(QStringLiteral("mcp"));
  window.set_mcp_server_name(name);
  mcp_action(window)->setChecked(true);

  const QByteArray bad_lines[] = {"not json\n", "{\"v\":2,\"op\":\"list\"}\n", "{\"v\":1,\"op\":\"x\"}\n",
                                   "{\"v\":1,\"op\":\"read\"}\n"};
  for (const QByteArray& line : bad_lines) {
    const auto reply = raw_request(name, line);
    QVERIFY(reply.has_value());
    QCOMPARE(*reply, QByteArray("{\"error\":\"bad_request\"}\n"));
  }

  const QByteArray oversize(70 * 1024, 'x');  
  const auto reply = raw_request(name, oversize, 3000);
  if (reply.has_value() && !reply->isEmpty()) QCOMPARE(*reply, QByteArray("{\"error\":\"bad_request\"}\n"));
}

void TestMcpServer::read_large_text_exact() {
  QTemporaryDir settings_dir;
  QTemporaryDir work_dir;
  QTemporaryDir socket_dir;
  QDir dir(work_dir.path());
  QString text;
  text.reserve(1024 * 1024 + 16);
  while (text.size() < 1024 * 1024) text += QStringLiteral("0123456789\n");
  const QString path = write_file(dir, QStringLiteral("big.md"), text);

  MainWindow window(QDir(settings_dir.path()).filePath(QStringLiteral("settings.ini")));
  const QString name = QDir(socket_dir.path()).filePath(QStringLiteral("mcp"));
  window.set_mcp_server_name(name);
  mcp_action(window)->setChecked(true);
  QVERIFY(window.open_file(path));

  const auto answer = ask(name, QJsonObject{{"v", 1}, {"op", "read"}, {"path", path}}, 2000, 10000);
  QVERIFY(answer.has_value());
  QCOMPARE(answer->value("text").toString(), text);
}

void TestMcpServer::switch_off_again_stops_listening() {
  QTemporaryDir settings_dir;
  QTemporaryDir socket_dir;
  MainWindow window(QDir(settings_dir.path()).filePath(QStringLiteral("settings.ini")));
  const QString name = QDir(socket_dir.path()).filePath(QStringLiteral("mcp"));
  window.set_mcp_server_name(name);
  QAction* action = mcp_action(window);
  action->setChecked(true);
  QVERIFY(window.mcp_listening());

  action->setChecked(false);
  QVERIFY(!window.mcp_listening());
  QVERIFY(!ask(name, QJsonObject{{"v", 1}, {"op", "list"}}, 300, 300).has_value());
}

void TestMcpServer::end_to_end_through_mcp_bridge() {
  QTemporaryDir home_dir;
  QTemporaryDir config_dir;
  QTemporaryDir state_dir;
  QTemporaryDir runtime_dir;
  QVERIFY(home_dir.isValid() && config_dir.isValid() && state_dir.isValid() && runtime_dir.isValid());

  const QString file = write_file(QDir(home_dir.path()), QStringLiteral("note.md"), QStringLiteral("hello\n"));

  QProcessEnvironment env;
  env.insert(QStringLiteral("HOME"), home_dir.path());
  env.insert(QStringLiteral("XDG_CONFIG_HOME"), config_dir.path());
  env.insert(QStringLiteral("XDG_STATE_HOME"), state_dir.path());
  env.insert(QStringLiteral("XDG_RUNTIME_DIR"), runtime_dir.path());

  QProcess process;
  process.setProcessEnvironment(env);
  process.start(QString::fromUtf8(LUMOS_MD_BINARY), {QStringLiteral("--mcp")});
  QVERIFY(process.waitForStarted(5000));

  const QString app_state_dir = QDir(state_dir.path()).filePath(QStringLiteral("lumos-md"));
  const QString name = instance_server_name(app_state_dir, runtime_dir.path(), QStringLiteral("lumos-md-mcp-"));

  MainWindow window(QDir(config_dir.path()).filePath(QStringLiteral("window-settings.ini")));
  window.set_mcp_server_name(name);
  mcp_action(window)->setChecked(true);
  QVERIFY(window.mcp_listening());
  QVERIFY(window.open_file(file));

  process.write(
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}\n");
  QVERIFY(process.waitForBytesWritten(2000));
  QTest::qWait(300);
  QCOMPARE(process.state(), QProcess::Running);

  const QByteArray call = QJsonDocument(QJsonObject{{"jsonrpc", "2.0"},
                                                      {"id", 2},
                                                      {"method", "tools/call"},
                                                      {"params", QJsonObject{{"name", "read_open_file"},
                                                                             {"arguments",
                                                                              QJsonObject{{"path", file}}}}}})
                               .toJson(QJsonDocument::Compact) +
                           '\n';
  process.write(call);
  QVERIFY(process.waitForBytesWritten(2000));

  QVERIFY(QTest::qWaitFor([&] { return process.canReadLine(); }, 5000));
  process.readLine();
  QVERIFY(QTest::qWaitFor([&] { return process.canReadLine(); }, 5000));
  const QByteArray line2 = process.readLine();
  QVERIFY(!process.canReadLine());

  QJsonParseError parse_error;
  const QJsonDocument doc = QJsonDocument::fromJson(line2, &parse_error);
  QCOMPARE(parse_error.error, QJsonParseError::NoError);
  const QJsonArray content = doc.object().value("result").toObject().value("content").toArray();
  QCOMPARE(content.size(), 1);
  QCOMPARE(content[0].toObject().value("text").toString(), QStringLiteral("hello\n"));

  process.closeWriteChannel();
  QVERIFY(process.waitForFinished(5000));
  QCOMPARE(process.exitCode(), 0);
  QVERIFY(process.readAllStandardOutput().isEmpty());
}

void TestMcpServer::mcp_extra_arg_exits_with_error() {
  QTemporaryDir home_dir;
  QProcessEnvironment env;
  env.insert(QStringLiteral("HOME"), home_dir.path());
  QProcess process;
  process.setProcessEnvironment(env);
  process.start(QString::fromUtf8(LUMOS_MD_BINARY), {QStringLiteral("--mcp"), QStringLiteral("extra-arg")});
  QVERIFY(process.waitForFinished(5000));
  QCOMPARE(process.exitCode(), 2);
  QVERIFY(process.readAllStandardOutput().isEmpty());
}

void TestMcpServer::socket_folder_pick_and_default() {
  QTemporaryDir settings_dir;
  QTemporaryDir socket_dir;
  QTemporaryDir picked_dir;
  QVERIFY(settings_dir.isValid() && socket_dir.isValid() && picked_dir.isValid());
  const QString settings_file = QDir(settings_dir.path()).filePath(QStringLiteral("settings.ini"));
  const QString default_name = QDir(socket_dir.path()).filePath(QStringLiteral("mcp"));
  const QString picked_name = mcp_socket_in(picked_dir.path());
  const QJsonObject list{{"v", 1}, {"op", "list"}};

  MainWindow window(settings_file);
  window.set_mcp_server_name(default_name);
  mcp_action(window)->setChecked(true);
  QAction* default_item = named_action(window, "mcp_socket_default");
  QAction* picked_item = named_action(window, "mcp_socket_picked");
  QVERIFY(default_item != nullptr && picked_item != nullptr);
  QVERIFY(default_item->isChecked());
  QVERIFY(!picked_item->isVisible());
  QVERIFY(ask(default_name, list).has_value());

  QString command;
  choose_socket_dir(window, picked_dir.path(), &command);
  QCOMPARE(command, QStringLiteral("lumos-md --mcp --mcp-dir ") + picked_dir.path());
  QVERIFY(picked_item->isVisible());
  QVERIFY(picked_item->isChecked());
  QCOMPARE(picked_item->text(), picked_dir.path());
  QVERIFY(window.mcp_listening());
  QVERIFY(ask(picked_name, list).has_value());
  QVERIFY(!ask(default_name, list, 300, 300).has_value());

  default_item->trigger();
  QVERIFY(default_item->isChecked());
  QVERIFY(ask(default_name, list).has_value());
  QVERIFY(!ask(picked_name, list, 300, 300).has_value());

  picked_item->trigger();
  QVERIFY(ask(picked_name, list).has_value());

  QTest::qWait(50);
  MainWindow second(settings_file);
  QVERIFY(named_action(second, "mcp_socket_picked")->isChecked());
  QCOMPARE(named_action(second, "mcp_socket_picked")->text(), picked_dir.path());
}

void TestMcpServer::socket_folder_never_removes_a_file() {
  QTemporaryDir settings_dir;
  QTemporaryDir socket_dir;
  QTemporaryDir picked_dir;
  QVERIFY(settings_dir.isValid() && socket_dir.isValid() && picked_dir.isValid());
  const QString file = write_file(QDir(picked_dir.path()), QStringLiteral("lumos-md-mcp.sock"), QStringLiteral("keep"));

  MainWindow window(QDir(settings_dir.path()).filePath(QStringLiteral("settings.ini")));
  window.set_mcp_server_name(QDir(socket_dir.path()).filePath(QStringLiteral("mcp")));
  mcp_action(window)->setChecked(true);
  QString command;
  choose_socket_dir(window, picked_dir.path(), &command);
  QVERIFY(!window.mcp_listening());
  QFile kept(file);
  QVERIFY(kept.open(QIODevice::ReadOnly));
  QCOMPARE(kept.readAll(), QByteArray("keep"));
}

void TestMcpServer::listen_replaces_a_stale_socket() {
  QTemporaryDir settings_dir;
  QTemporaryDir socket_dir;
  QVERIFY(settings_dir.isValid() && socket_dir.isValid());
  MainWindow window(QDir(settings_dir.path()).filePath(QStringLiteral("settings.ini")));
  const QString name = mcp_socket_in(socket_dir.path());
  McpServer first(window);
  QVERIFY(first.listen(name));
  McpServer second(window);
  QVERIFY(second.listen(name));
  QVERIFY(ask(name, QJsonObject{{"v", 1}, {"op", "list"}}).has_value());
}

void TestMcpServer::end_to_end_with_mcp_dir() {
  QTemporaryDir home_dir;
  QTemporaryDir picked_dir;
  QVERIFY(home_dir.isValid() && picked_dir.isValid());
  QProcessEnvironment env;
  env.insert(QStringLiteral("HOME"), home_dir.path());
  env.insert(QStringLiteral("XDG_CONFIG_HOME"), QDir(home_dir.path()).filePath(QStringLiteral("config")));
  env.insert(QStringLiteral("XDG_STATE_HOME"), QDir(home_dir.path()).filePath(QStringLiteral("state")));

  QString command;
  MainWindow window(QDir(home_dir.path()).filePath(QStringLiteral("window-settings.ini")));
  window.set_mcp_server_name(QDir(home_dir.path()).filePath(QStringLiteral("default-mcp")));
  mcp_action(window)->setChecked(true);
  choose_socket_dir(window, picked_dir.path(), &command);
  QVERIFY(window.mcp_listening());

  QProcess process;
  process.setProcessEnvironment(env);
  process.start(QString::fromUtf8(LUMOS_MD_BINARY),
                {QStringLiteral("--mcp"), QStringLiteral("--mcp-dir"), picked_dir.path()});
  QVERIFY(process.waitForStarted(5000));
  process.write("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"list_open_files\"}}\n");
  QVERIFY(process.waitForBytesWritten(2000));
  QVERIFY(QTest::qWaitFor([&] { return process.canReadLine(); }, 5000));
  const QJsonObject result = QJsonDocument::fromJson(process.readLine()).object().value("result").toObject();
  QCOMPARE(result.value("isError").toBool(true), false);
  process.closeWriteChannel();
  QVERIFY(process.waitForFinished(5000));
  QCOMPARE(process.exitCode(), 0);
}

QTEST_MAIN(TestMcpServer)
#include "test_mcp_server.moc"
