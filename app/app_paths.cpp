#include "app_paths.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include "lumos/log.hpp"

namespace {

constexpr auto kSettingsName = "lumos-md.ini";
constexpr auto kLogName = "lumos-md.log";

}  

QString AppPaths::settings_file() const { return QDir(config_dir).filePath(QString::fromLatin1(kSettingsName)); }

QString AppPaths::log_file() const { return QDir(state_dir).filePath(QString::fromLatin1(kLogName)); }

AppPaths find_app_paths() {
  AppPaths p;
  const QString home = QDir(QDir::homePath()).filePath(QStringLiteral(".lumos-md"));
  if (QFileInfo(QDir(home).filePath(QString::fromLatin1(kSettingsName))).isFile()) {
    p.config_dir = home;
    p.state_dir = home;
    p.home_mode = true;
  } else {
    p.config_dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    p.state_dir = QStandardPaths::writableLocation(QStandardPaths::StateLocation);
  }
  return p;
}

void log_app_paths(const AppPaths& p) {
  lumos::log(lumos::LogLevel::debug, "app", "paths",
             {{"mode", p.home_mode ? "home" : "xdg"},
              {"config", p.config_dir.toStdString()},
              {"state", p.state_dir.toStdString()}});
}
