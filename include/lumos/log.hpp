#pragma once

#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace lumos {

enum class LogLevel { debug, info, warn, error };

using LogSink = std::function<void(LogLevel, std::string_view line)>;

void set_log_sink(LogSink sink);
void set_log_level(LogLevel level);
bool log_enabled(LogLevel level);

using LogField = std::pair<std::string_view, std::string_view>;

void log(LogLevel level, std::string_view comp, std::string_view event,
         std::initializer_list<LogField> fields = {});

}  
