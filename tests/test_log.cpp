#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "lumos/log.hpp"

TEST_CASE("log writes key=value lines and respects the level") {
  std::vector<std::string> lines;
  lumos::set_log_sink([&](lumos::LogLevel, std::string_view line) { lines.emplace_back(line); });

  lumos::set_log_level(lumos::LogLevel::info);
  lumos::log(lumos::LogLevel::debug, "test", "hidden");
  CHECK(lines.empty());

  lumos::set_log_level(lumos::LogLevel::debug);
  lumos::log(lumos::LogLevel::debug, "test", "shown", {{"note", "a b=\"c\""}});
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].find(" level=debug comp=test event=shown note=\"a b=\\\"c\\\"\"") !=
        std::string::npos);

  lumos::set_log_sink(nullptr);
  lumos::set_log_level(lumos::LogLevel::info);
}
