#pragma once

#include <QString>

struct AppPaths {
  QString config_dir;  
  QString state_dir;   
  bool home_mode = false;  

  QString settings_file() const;
  QString log_file() const;  
};

AppPaths find_app_paths();

void log_app_paths(const AppPaths& p);
