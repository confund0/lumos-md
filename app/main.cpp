
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>

#include <memory>
#include <string>

#include "app_log.hpp"
#include "app_paths.hpp"
#include "lumos/log.hpp"
#include "main_window.hpp"
#include "single_instance.hpp"

int main(int argc, char** argv) {
  const QString activation_token = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
  QApplication app(argc, argv);
  QApplication::setApplicationName("lumos-md");
  QGuiApplication::setDesktopFileName("lumos-md");

  QCommandLineParser parser;
  parser.setApplicationDescription("lumos-md Markdown editor");
  parser.addHelpOption();
  const QCommandLineOption debug("debug", "Write debug log lines, also to stderr.");
  parser.addOption(debug);
  parser.addPositionalArgument("file", "Markdown files to open, each in its own tab.", "[file...]");
  parser.process(app);

  const AppPaths paths = find_app_paths();
  const bool debug_setting = QSettings(paths.settings_file(), QSettings::IniFormat).value("log/debug", false).toBool();
  install_log({.path = paths.log_file(), .also_stderr = parser.isSet(debug)});
  if (debug_setting || parser.isSet(debug)) lumos::set_log_level(lumos::LogLevel::debug);
  lumos::log(lumos::LogLevel::info, "app", "start",
             {{"pid", std::to_string(QCoreApplication::applicationPid())},
              {"debug", parser.isSet(debug) ? "flag" : debug_setting ? "setting" : "off"}});
  log_app_paths(paths);

  const QString server_name = instance_server_name(paths.state_dir, qEnvironmentVariable("XDG_RUNTIME_DIR"));
  QStringList files = parser.positionalArguments();
  for (QString& file : files) file = QFileInfo(file).absoluteFilePath();

  const SendResult send_result = send_to_running(server_name, files, activation_token, 2000);
  const char* result_name =
      send_result == SendResult::sent ? "sent" : send_result == SendResult::no_server ? "no_server" : "failed";
  lumos::log(lumos::LogLevel::debug, "app", "instance_client",
             {{"result", result_name},
              {"paths", std::to_string(files.size())},
              {"token", activation_token.isEmpty() ? "0" : "1"}});
  if (send_result == SendResult::sent) {
    uninstall_log();
    return 0;
  }

  MainWindow window(paths.settings_file(), paths.open_tabs_file(), paths.drafts_dir());
  window.show();

  std::unique_ptr<InstanceServer> server;
  if (send_result == SendResult::no_server) {
    server = std::make_unique<InstanceServer>(
        [&window](const QStringList& p, const QString& token) { window.open_from_other_start(p, token); });
    server->listen(server_name);
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, &app, [&server] { server->close(); });
  }

  window.restore_tabs();
  for (const QString& file : files) window.open_file(file);

  const int code = QApplication::exec();
  uninstall_log();
  return code;
}
