
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>
#include <future>
#include <thread>

#include "document_tab.hpp"
#include "main_window.hpp"
#include "single_instance.hpp"

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

template <typename T>
T wait_for(std::future<T>& future) {
  while (future.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready) QCoreApplication::processEvents();
  return future.get();
}

}  

class TestSingleInstance : public QObject {
  Q_OBJECT

 private slots:
  void names();
  void no_server_gives_no_server();
  void server_receives_paths_in_order();
  void zero_paths_sent_and_called_once();
  void hung_server_gives_failed();
  void bad_message_gives_failed();
  void token_reaches_server();
  void bad_token_sent_as_empty();
  void newline_in_path_gives_failed();
  void stale_socket_file_listen_succeeds();
  void window_opens_two_tabs_and_dedupes();
  void window_queues_while_modal_open();
  void window_unsets_token_after_raise();
};

void TestSingleInstance::names() {
  QTemporaryDir state_a;
  QTemporaryDir state_b;
  QTemporaryDir runtime;
  QVERIFY(state_a.isValid() && state_b.isValid() && runtime.isValid());

  const QString n1 = instance_server_name(state_a.path(), QString());
  QVERIFY(n1.startsWith(QStringLiteral("lumos-md-")));
  QCOMPARE(instance_server_name(state_a.path(), QString()), n1);
  QVERIFY(instance_server_name(state_b.path(), QString()) != n1);

  const QString with_runtime = instance_server_name(state_a.path(), runtime.path());
  QVERIFY(with_runtime.startsWith(runtime.path()));
  QVERIFY(with_runtime.endsWith(n1));

  const QString missing_runtime = QDir(runtime.path()).filePath(QStringLiteral("missing"));
  QCOMPARE(instance_server_name(state_a.path(), missing_runtime), n1);

  QDir base(state_a.path());
  QVERIFY(base.mkpath(QStringLiteral("a/b")));
  QVERIFY(base.mkpath(QStringLiteral("a/c")));
  QCOMPARE(instance_server_name(base.filePath(QStringLiteral("a/b/../c")), QString()),
           instance_server_name(base.filePath(QStringLiteral("a/c")), QString()));
}

void TestSingleInstance::no_server_gives_no_server() {
  QTemporaryDir dir;
  const QString name = QDir(dir.path()).filePath(QStringLiteral("no-such-socket"));
  QCOMPARE(send_to_running(name, {}, {}, 300), SendResult::no_server);
}

void TestSingleInstance::server_receives_paths_in_order() {
  QTemporaryDir dir;
  const QString name = QDir(dir.path()).filePath(QStringLiteral("srv-order"));
  QStringList received;
  InstanceServer server([&received](const QStringList& p, const QString&) { received = p; });
  QVERIFY(server.listen(name));

  const QStringList paths = {"/a/one", "/a/two", "/a/three"};
  std::packaged_task<SendResult()> task([&] { return send_to_running(name, paths, {}, 2000); });
  auto future = task.get_future();
  std::thread thread(std::move(task));
  QCOMPARE(wait_for(future), SendResult::sent);
  thread.join();
  QCOMPARE(received, paths);
}

void TestSingleInstance::zero_paths_sent_and_called_once() {
  QTemporaryDir dir;
  const QString name = QDir(dir.path()).filePath(QStringLiteral("srv-zero"));
  int calls = 0;
  QStringList received = {QStringLiteral("unset")};
  InstanceServer server([&](const QStringList& p, const QString&) {
    ++calls;
    received = p;
  });
  QVERIFY(server.listen(name));

  std::packaged_task<SendResult()> task([&] { return send_to_running(name, {}, {}, 2000); });
  auto future = task.get_future();
  std::thread thread(std::move(task));
  QCOMPARE(wait_for(future), SendResult::sent);
  thread.join();
  QCOMPARE(calls, 1);
  QVERIFY(received.isEmpty());
}

void TestSingleInstance::hung_server_gives_failed() {
  QTemporaryDir dir;
  const QString name = QDir(dir.path()).filePath(QStringLiteral("hung"));
  QLocalServer raw;
  QVERIFY(raw.listen(name));
  QObject::connect(&raw, &QLocalServer::newConnection, &raw, [&raw] {
    while (QLocalSocket* s = raw.nextPendingConnection()) s->setParent(&raw);
  });

  QElapsedTimer clock;
  clock.start();
  std::packaged_task<SendResult()> task([&] { return send_to_running(name, {}, {}, 300); });
  auto future = task.get_future();
  std::thread thread(std::move(task));
  QCOMPARE(wait_for(future), SendResult::failed);
  thread.join();
  QVERIFY(clock.elapsed() < 2000);
}

void TestSingleInstance::bad_message_gives_failed() {
  QTemporaryDir dir;
  const QString name = QDir(dir.path()).filePath(QStringLiteral("srv-bad"));
  int calls = 0;
  InstanceServer server([&calls](const QStringList&, const QString&) { ++calls; });
  QVERIFY(server.listen(name));

  const QByteArray raw_messages[] = {"not-the-header\n\n", "lumos-md-open 1\n/x\n\n", "lumos-md-open 2\n/x\n\n",
                                     "lumos-md-open 2\ntoken=a\tb\n/x\n\n"};
  for (const QByteArray& raw : raw_messages) {
    std::packaged_task<bool()> header_task([&] {
      QLocalSocket socket;
      socket.connectToServer(name);
      if (!socket.waitForConnected(2000)) return false;
      socket.write(raw);
      socket.waitForBytesWritten(2000);
      return socket.waitForDisconnected(2000) && socket.readAll().isEmpty();
    });
    auto header_future = header_task.get_future();
    std::thread header_thread(std::move(header_task));
    QVERIFY2(wait_for(header_future), raw.constData());
    header_thread.join();
    QCOMPARE(calls, 0);
  }

  std::packaged_task<SendResult()> relative_task(
      [&] { return send_to_running(name, {QStringLiteral("relative/path")}, {}, 2000); });
  auto relative_future = relative_task.get_future();
  std::thread relative_thread(std::move(relative_task));
  QCOMPARE(wait_for(relative_future), SendResult::failed);
  relative_thread.join();
  QCOMPARE(calls, 0);
}

void TestSingleInstance::token_reaches_server() {
  QTemporaryDir dir;
  const QString name = QDir(dir.path()).filePath(QStringLiteral("srv-token"));
  QStringList received;
  QString token = QStringLiteral("unset");
  InstanceServer server([&](const QStringList& p, const QString& t) {
    received = p;
    token = t;
  });
  QVERIFY(server.listen(name));

  const QStringList paths = {"/a/one", "/a/two"};
  const QString sent = QStringLiteral("kde-1_2.3:x");
  std::packaged_task<SendResult()> task([&] { return send_to_running(name, paths, sent, 2000); });
  auto future = task.get_future();
  std::thread thread(std::move(task));
  QCOMPARE(wait_for(future), SendResult::sent);
  thread.join();
  QCOMPARE(received, paths);
  QCOMPARE(token, sent);

  std::packaged_task<SendResult()> empty_task([&] { return send_to_running(name, {}, {}, 2000); });
  auto empty_future = empty_task.get_future();
  std::thread empty_thread(std::move(empty_task));
  QCOMPARE(wait_for(empty_future), SendResult::sent);
  empty_thread.join();
  QVERIFY(received.isEmpty());
  QVERIFY(token.isEmpty());
}

void TestSingleInstance::bad_token_sent_as_empty() {
  QTemporaryDir dir;
  const QString name = QDir(dir.path()).filePath(QStringLiteral("srv-bad-token"));
  QStringList received;
  QString token = QStringLiteral("unset");
  InstanceServer server([&](const QStringList& p, const QString& t) {
    received = p;
    token = t;
  });
  QVERIFY(server.listen(name));

  for (const QString& bad : {QStringLiteral("a\nb"), QString(4097, QLatin1Char('t'))}) {
    token = QStringLiteral("unset");
    std::packaged_task<SendResult()> task([&] { return send_to_running(name, {QStringLiteral("/x")}, bad, 2000); });
    auto future = task.get_future();
    std::thread thread(std::move(task));
    QCOMPARE(wait_for(future), SendResult::sent);
    thread.join();
    QCOMPARE(received, QStringList{QStringLiteral("/x")});
    QVERIFY(token.isEmpty());
  }
}

void TestSingleInstance::newline_in_path_gives_failed() {
  QTemporaryDir dir;
  const QString name = QDir(dir.path()).filePath(QStringLiteral("srv-newline"));
  QLocalServer server;
  QVERIFY(server.listen(name));
  int connections = 0;
  QObject::connect(&server, &QLocalServer::newConnection, &server, [&connections] { ++connections; });

  std::packaged_task<SendResult()> task([&] { return send_to_running(name, {QStringLiteral("/a\nb")}, {}, 2000); });
  auto future = task.get_future();
  std::thread thread(std::move(task));
  QCOMPARE(wait_for(future), SendResult::failed);
  thread.join();
  QCoreApplication::processEvents();
  QCOMPARE(connections, 0);
}

void TestSingleInstance::stale_socket_file_listen_succeeds() {
  QTemporaryDir dir;
  const QString name = QDir(dir.path()).filePath(QStringLiteral("stale"));
  QFile stale(name);
  QVERIFY(stale.open(QIODevice::WriteOnly));
  stale.write("not a socket");
  stale.close();

  QStringList received;
  InstanceServer server([&received](const QStringList& p, const QString&) { received = p; });
  QVERIFY(server.listen(name));

  std::packaged_task<SendResult()> task([&] { return send_to_running(name, {QStringLiteral("/x")}, {}, 2000); });
  auto future = task.get_future();
  std::thread thread(std::move(task));
  QCOMPARE(wait_for(future), SendResult::sent);
  thread.join();
  QCOMPARE(received, QStringList{QStringLiteral("/x")});
}

void TestSingleInstance::window_opens_two_tabs_and_dedupes() {
  QTemporaryDir dir;
  MainWindow window(QDir(dir.path()).filePath(QStringLiteral("settings.ini")));
  const QString a = write_file(QDir(dir.path()), QStringLiteral("a.md"), QStringLiteral("# A\n"));
  const QString b = write_file(QDir(dir.path()), QStringLiteral("b.md"), QStringLiteral("# B\n"));
  QVERIFY(window.open_file(a));
  QCOMPARE(window.tab_count(), 1);

  window.open_from_other_start({a, b});
  QCOMPARE(window.tab_count(), 1);  
  QVERIFY(QTest::qWaitFor([&] { return window.tab_count() == 2; }, 5000));
  QCOMPARE(window.tab_count(), 2);
  QCOMPARE(window.tab_at(0)->path(), a);
  QCOMPARE(window.tab_at(1)->path(), b);
  QCOMPARE(window.current_tab()->path(), b);

  window.open_from_other_start({a});
  QVERIFY(QTest::qWaitFor([&] { return window.current_tab()->path() == a; }, 5000));
  QCOMPARE(window.current_tab()->path(), a);
  QCOMPARE(window.tab_count(), 2);
}

void TestSingleInstance::window_queues_while_modal_open() {
  QTemporaryDir dir;
  MainWindow window(QDir(dir.path()).filePath(QStringLiteral("settings.ini")));
  const QString a = write_file(QDir(dir.path()), QStringLiteral("a.md"), QStringLiteral("# A\n"));

  QDialog dialog(&window);
  dialog.open();
  QVERIFY(QApplication::activeModalWidget() != nullptr);

  window.open_from_other_start({a});
  QVERIFY(window.current_tab()->is_blank());

  dialog.close();
  QVERIFY(QTest::qWaitFor([&] { return !window.current_tab()->is_blank(); }, 5000));
  QCOMPARE(window.current_tab()->path(), a);
}

void TestSingleInstance::window_unsets_token_after_raise() {
  QTemporaryDir dir;
  MainWindow window(QDir(dir.path()).filePath(QStringLiteral("settings.ini")));
  window.show();

  qputenv("XDG_ACTIVATION_TOKEN", "old");
  window.open_from_other_start({}, QStringLiteral("new"));
  QVERIFY(QTest::qWaitFor([] { return !qEnvironmentVariableIsSet("XDG_ACTIVATION_TOKEN"); }, 5000));

  qputenv("XDG_ACTIVATION_TOKEN", "old");
  window.open_from_other_start({});
  QVERIFY(QTest::qWaitFor([] { return !qEnvironmentVariableIsSet("XDG_ACTIVATION_TOKEN"); }, 5000));
}

QTEST_MAIN(TestSingleInstance)
#include "test_single_instance.moc"
