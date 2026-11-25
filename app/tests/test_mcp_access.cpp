
#include <catch2/catch_test_macros.hpp>

#include "mcp_access.hpp"

TEST_CASE("mcp_path_allowed", "[mcp_access]") {
  SECTION("exact folder") { REQUIRE(mcp_path_allowed(QStringLiteral("/a/docs"), {QStringLiteral("/a/docs")})); }

  SECTION("file inside the folder") {
    REQUIRE(mcp_path_allowed(QStringLiteral("/a/docs/notes.md"), {QStringLiteral("/a/docs")}));
  }

  SECTION("file nested under the folder") {
    REQUIRE(mcp_path_allowed(QStringLiteral("/a/docs/sub/notes.md"), {QStringLiteral("/a/docs")}));
  }

  SECTION("a sibling with the folder name as a prefix does not match") {
    REQUIRE_FALSE(mcp_path_allowed(QStringLiteral("/a/docs-x/f.md"), {QStringLiteral("/a/docs")}));
  }

  SECTION("the root folder allows every key") {
    REQUIRE(mcp_path_allowed(QStringLiteral("/a/docs/f.md"), {QStringLiteral("/")}));
  }

  SECTION("an empty folder list allows nothing") { REQUIRE_FALSE(mcp_path_allowed(QStringLiteral("/a/docs"), {})); }

  SECTION("an empty key is never allowed") {
    REQUIRE_FALSE(mcp_path_allowed(QString(), {QStringLiteral("/a/docs")}));
  }
}
