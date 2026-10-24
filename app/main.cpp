
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QSettings>

#include <string>

#include "app_log.hpp"
#include "app_paths.hpp"
#include "lumos/log.hpp"
#include "main_window.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("lumos-md");
  QGuiApplication::setDesktopFileName("lumos-md");

  QCommandLineParser parser;
  parser.setApplicationDescription("lumos-md Markdown editor");
  parser.addHelpOption();
  const QCommandLineOption debug("debug", "Write debug log lines, also to stderr.");
  parser.addOption(debug);
  parser.addPositionalArgument("file", "Markdown file to open.", "[file]");
  parser.process(app);

  const AppPaths paths = find_app_paths();
  const bool debug_setting = QSettings(paths.settings_file(), QSettings::IniFormat).value("log/debug", false).toBool();
  install_log({.path = paths.log_file(), .also_stderr = parser.isSet(debug)});
  if (debug_setting || parser.isSet(debug)) lumos::set_log_level(lumos::LogLevel::debug);
  lumos::log(lumos::LogLevel::info, "app", "start",
             {{"pid", std::to_string(QCoreApplication::applicationPid())},
              {"debug", parser.isSet(debug) ? "flag" : debug_setting ? "setting" : "off"}});
  log_app_paths(paths);
  MainWindow window(paths.settings_file());
  const QStringList files = parser.positionalArguments();
  if (!files.isEmpty()) window.load(files.first());
  window.show();
  const int code = QApplication::exec();
  uninstall_log();
  return code;
}
