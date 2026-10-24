#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "lumos/text_source.hpp"
#include "lumos/types.hpp"

using lumos::Edit;
using lumos::StringSource;
using lumos::TextRegion;

TEST_CASE("region_of gives the range of one edit") {
  CHECK(lumos::region_of(Edit{3, 2, "abc"}) == TextRegion{3, 5, 6});
  CHECK(lumos::region_of(Edit{0, 0, ""}) == TextRegion{0, 0, 0});
}

TEST_CASE("merge_edits covers all changes") {
  std::mt19937 rng(7);
  for (int round = 0; round < 500; ++round) {
    std::string text = "0123456789abcdefghij";
    const std::string before = text;
    std::vector<Edit> edits;
    const int count = std::uniform_int_distribution<int>(1, 4)(rng);
    for (int k = 0; k < count; ++k) {
      const std::size_t off = std::uniform_int_distribution<std::size_t>(0, text.size())(rng);
      const std::size_t rem =
          std::uniform_int_distribution<std::size_t>(0, std::min<std::size_t>(5, text.size() - off))(rng);
      const std::string ins(std::uniform_int_distribution<std::size_t>(0, 4)(rng), 'X');
      edits.push_back(Edit{off, rem, ins});
      text.replace(off, rem, ins);
    }
    const TextRegion r = lumos::merge_edits(edits);
    REQUIRE(r.start <= r.new_end);
    REQUIRE(r.new_end <= text.size());
    REQUIRE(r.start <= r.old_end);
    REQUIRE(r.old_end <= before.size());
    CHECK(text.substr(0, r.start) == before.substr(0, r.start));
    CHECK(text.substr(r.new_end) == before.substr(r.old_end));
  }
  CHECK(lumos::merge_edits({}) == TextRegion{});
}

TEST_CASE("string source reads chunks, slices and lines") {
  const std::string text = "one\r\ntwo\n\nlast";
  const StringSource src(text);
  CHECK(src.size() == text.size());
  CHECK(src.chunk_at(5) == "two\n\nlast");
  CHECK(src.chunk_at(text.size()).empty());
  CHECK(src.slice(5, 3) == "two");
  CHECK(src.line_count() == 4);
  CHECK(src.line_start(1) == 5);
  CHECK(src.line_start(3) == 10);
  CHECK(src.line_of(4) == 0);
  CHECK(src.line_of(5) == 1);
  CHECK(src.line_of(text.size()) == 3);
  CHECK_THROWS_AS(src.chunk_at(text.size() + 1), std::out_of_range);
  CHECK_THROWS_AS(src.slice(10, 5), std::out_of_range);
  CHECK_THROWS_AS(src.line_start(4), std::out_of_range);
  CHECK_THROWS_AS(src.line_of(text.size() + 1), std::out_of_range);
}
