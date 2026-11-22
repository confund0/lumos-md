
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "app_log.hpp"
#include "app_paths.hpp"
#include "lumos/log.hpp"
#include "main_window.hpp"
#include "mcp_bridge.hpp"
#include "single_instance.hpp"

namespace {

bool has_mcp_flag(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--mcp") return true;
  }
  return false;
}

AppPaths start_log(bool debug_flag) {
  const AppPaths paths = find_app_paths();
  const bool debug_setting = QSettings(paths.settings_file(), QSettings::IniFormat).value("log/debug", false).toBool();
  install_log({.path = paths.log_file(), .also_stderr = debug_flag});
  if (debug_setting || debug_flag) lumos::set_log_level(lumos::LogLevel::debug);
  lumos::log(lumos::LogLevel::info, "app", "start",
             {{"pid", std::to_string(QCoreApplication::applicationPid())},
              {"debug", debug_flag ? "flag" : debug_setting ? "setting" : "off"}});
  log_app_paths(paths);
  return paths;
}

int run_as_mcp_bridge(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName("lumos-md");

  QCommandLineParser parser;
  parser.setApplicationDescription("lumos-md Markdown editor");
  parser.addHelpOption();
  const QCommandLineOption mcp("mcp", "Run as the MCP bridge for an AI agent, over stdin and stdout.");
  parser.addOption(mcp);
  const QCommandLineOption debug("debug", "Write debug log lines, also to stderr.");
  parser.addOption(debug);
  const QCommandLineOption mcp_dir("mcp-dir", "The socket folder set in Tools, MCP Socket Folder.", "dir");
  parser.addOption(mcp_dir);
  parser.process(app);

  if (!parser.positionalArguments().isEmpty()) {
    std::cerr << "lumos-md --mcp takes no file arguments." << std::endl;
    return 2;
  }

  const AppPaths paths = start_log(parser.isSet(debug));
  if (parser.isSet(mcp_dir)) return run_mcp_bridge(mcp_socket_in(QDir(parser.value(mcp_dir)).absolutePath()));
  return run_mcp_bridge(
      instance_server_name(paths.state_dir, qEnvironmentVariable("XDG_RUNTIME_DIR"), QStringLiteral("lumos-md-mcp-")));
}

}  

int main(int argc, char** argv) {
  if (has_mcp_flag(argc, argv)) return run_as_mcp_bridge(argc, argv);

  const QString activation_token = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
  QApplication app(argc, argv);
  QApplication::setApplicationName("lumos-md");
  QGuiApplication::setDesktopFileName("lumos-md");

  QCommandLineParser parser;
  parser.setApplicationDescription("lumos-md Markdown editor");
  parser.addHelpOption();
  const QCommandLineOption mcp("mcp", "Run as the MCP bridge for an AI agent, over stdin and stdout.");
  parser.addOption(mcp);
  const QCommandLineOption debug("debug", "Write debug log lines, also to stderr.");
  parser.addOption(debug);
  parser.addPositionalArgument("file", "Markdown files to open, each in its own tab.", "[file...]");
  parser.process(app);

  const AppPaths paths = start_log(parser.isSet(debug));

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
    window.set_mcp_server_name(
        instance_server_name(paths.state_dir, qEnvironmentVariable("XDG_RUNTIME_DIR"), QStringLiteral("lumos-md-mcp-")));
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, &app, [&server] { server->close(); });
  }

  window.restore_tabs();
  for (const QString& file : files) window.open_file(file);

  const int code = QApplication::exec();
  uninstall_log();
  return code;
}
