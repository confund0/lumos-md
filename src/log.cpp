#include "lumos/log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <memory>
#include <mutex>

namespace lumos {
namespace {

std::mutex g_mutex;
std::shared_ptr<const LogSink> g_sink;
std::atomic<int> g_level{static_cast<int>(LogLevel::info)};

std::string_view level_name(LogLevel level) {
  switch (level) {
    case LogLevel::debug: return "debug";
    case LogLevel::info: return "info";
    case LogLevel::warn: return "warn";
    case LogLevel::error: return "error";
  }
  return "unknown";
}

bool needs_quotes(std::string_view value) {
  if (value.empty()) return true;
  for (char c : value) {
    if (c == ' ' || c == '"' || c == '=' || c == '\\' || c == '\n' || c == '\t') return true;
  }
  return false;
}

void append_value(std::string& out, std::string_view value) {
  if (!needs_quotes(value)) {
    out += value;
    return;
  }
  out += '"';
  for (char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  out += '"';
}

std::string timestamp() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
  const std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  const std::size_t n = std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
  char out[48];
  std::snprintf(out, sizeof out, "%.*s.%03dZ", static_cast<int>(n), buf, static_cast<int>(ms));
  return out;
}

}  

void set_log_sink(LogSink sink) {
  auto ptr = sink ? std::make_shared<const LogSink>(std::move(sink)) : nullptr;
  const std::lock_guard lock(g_mutex);
  g_sink = std::move(ptr);
}

void set_log_level(LogLevel level) { g_level.store(static_cast<int>(level)); }

bool log_enabled(LogLevel level) { return static_cast<int>(level) >= g_level.load(); }

void log(LogLevel level, std::string_view comp, std::string_view event,
         std::initializer_list<LogField> fields) {
  if (!log_enabled(level)) return;
  std::shared_ptr<const LogSink> sink;
  {
    const std::lock_guard lock(g_mutex);
    sink = g_sink;
  }
  if (!sink) return;

  std::string line = "ts=" + timestamp();
  line += " level=";
  line += level_name(level);
  line += " comp=";
  append_value(line, comp);
  line += " event=";
  append_value(line, event);
  for (const auto& [key, value] : fields) {
    line += ' ';
    line += key;
    line += '=';
    append_value(line, value);
  }
  (*sink)(level, line);
}

}  
