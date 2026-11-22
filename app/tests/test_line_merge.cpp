#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "line_merge.hpp"

namespace {

std::vector<std::string> make_lines(int n) {
  std::vector<std::string> lines;
  lines.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) lines.push_back("line" + std::to_string(i) + "\n");
  return lines;
}

std::string join(const std::vector<std::string>& lines) {
  std::string out;
  for (const std::string& line : lines) out += line;
  return out;
}

void check_coverage(const LineMerge& merge) {
  if (merge.too_many) {
    CHECK(merge.chunks.empty());
    return;
  }
  int pos_base = 0, pos_yours = 0, pos_other = 0;
  for (const MergeChunk& chunk : merge.chunks) {
    CHECK(chunk.base_begin == pos_base);
    CHECK(chunk.yours_begin == pos_yours);
    CHECK(chunk.other_begin == pos_other);
    pos_base = chunk.base_end;
    pos_yours = chunk.yours_end;
    pos_other = chunk.other_end;
  }
  CHECK(pos_base == static_cast<int>(merge.base.size()));
  CHECK(pos_yours == static_cast<int>(merge.yours.size()));
  CHECK(pos_other == static_cast<int>(merge.other.size()));
}

}  

TEST_CASE("line_merge: split_lines") {
  CHECK(split_lines("").empty());
  CHECK(split_lines("a") == std::vector<std::string_view>{"a"});
  CHECK(split_lines("a\n") == std::vector<std::string_view>{"a\n"});
  CHECK(split_lines("a\nb") == std::vector<std::string_view>{"a\n", "b"});
  CHECK(split_lines("a\r\nb\n") == std::vector<std::string_view>{"a\r\n", "b\n"});
  CHECK(split_lines("\n\n") == std::vector<std::string_view>{"\n", "\n"});
}

TEST_CASE("line_merge: diff_lines equal") {
  std::vector<std::string_view> a{"a\n", "b\n", "c\n"};
  std::vector<LineHunk> out;
  REQUIRE(diff_lines(a, a, kMergeCap, &out));
  CHECK(out.empty());
}

TEST_CASE("line_merge: diff_lines insert at start") {
  std::vector<std::string_view> a{"b\n", "c\n"};
  std::vector<std::string_view> b{"x\n", "b\n", "c\n"};
  std::vector<LineHunk> out;
  REQUIRE(diff_lines(a, b, kMergeCap, &out));
  REQUIRE(out.size() == 1);
  CHECK(out[0].a_begin == 0);
  CHECK(out[0].a_end == 0);
  CHECK(out[0].b_begin == 0);
  CHECK(out[0].b_end == 1);
}

TEST_CASE("line_merge: diff_lines insert in middle") {
  std::vector<std::string_view> a{"a\n", "c\n"};
  std::vector<std::string_view> b{"a\n", "x\n", "c\n"};
  std::vector<LineHunk> out;
  REQUIRE(diff_lines(a, b, kMergeCap, &out));
  REQUIRE(out.size() == 1);
  CHECK(out[0].a_begin == 1);
  CHECK(out[0].a_end == 1);
  CHECK(out[0].b_begin == 1);
  CHECK(out[0].b_end == 2);
}

TEST_CASE("line_merge: diff_lines insert at end") {
  std::vector<std::string_view> a{"a\n", "b\n"};
  std::vector<std::string_view> b{"a\n", "b\n", "x\n"};
  std::vector<LineHunk> out;
  REQUIRE(diff_lines(a, b, kMergeCap, &out));
  REQUIRE(out.size() == 1);
  CHECK(out[0].a_begin == 2);
  CHECK(out[0].a_end == 2);
  CHECK(out[0].b_begin == 2);
  CHECK(out[0].b_end == 3);
}

TEST_CASE("line_merge: diff_lines delete") {
  std::vector<std::string_view> a{"a\n", "b\n", "c\n"};
  std::vector<std::string_view> b{"a\n", "c\n"};
  std::vector<LineHunk> out;
  REQUIRE(diff_lines(a, b, kMergeCap, &out));
  REQUIRE(out.size() == 1);
  CHECK(out[0].a_begin == 1);
  CHECK(out[0].a_end == 2);
  CHECK(out[0].b_begin == 1);
  CHECK(out[0].b_end == 1);
}

TEST_CASE("line_merge: diff_lines replace") {
  std::vector<std::string_view> a{"a\n", "b\n", "c\n"};
  std::vector<std::string_view> b{"a\n", "x\n", "c\n"};
  std::vector<LineHunk> out;
  REQUIRE(diff_lines(a, b, kMergeCap, &out));
  REQUIRE(out.size() == 1);
  CHECK(out[0].a_begin == 1);
  CHECK(out[0].a_end == 2);
  CHECK(out[0].b_begin == 1);
  CHECK(out[0].b_end == 2);
}

TEST_CASE("line_merge: diff_lines line end only differs") {
  std::vector<std::string_view> lf{"a\n"};
  std::vector<std::string_view> crlf{"a\r\n"};
  std::vector<std::string_view> no_end{"a"};
  std::vector<LineHunk> out;

  REQUIRE(diff_lines(lf, crlf, kMergeCap, &out));
  REQUIRE(out.size() == 1);
  CHECK(out[0].a_begin == 0);
  CHECK(out[0].a_end == 1);
  CHECK(out[0].b_begin == 0);
  CHECK(out[0].b_end == 1);

  REQUIRE(diff_lines(lf, no_end, kMergeCap, &out));
  REQUIRE(out.size() == 1);

  REQUIRE(diff_lines(lf, lf, kMergeCap, &out));
  CHECK(out.empty());
}

TEST_CASE("line_merge: merge_lines equal texts") {
  const std::string base = "a\nb\nc\n";
  LineMerge merge = merge_lines(base, base, base);
  CHECK_FALSE(merge.too_many);
  REQUIRE(merge.chunks.size() == 1);
  CHECK(merge.chunks[0].kind == ChunkKind::same);
  CHECK(merge.conflicts() == 0);
  CHECK(merge_result(merge, default_picks(merge)) == base);
}

TEST_CASE("line_merge: merge_lines yours only change") {
  const std::string base = "a\nb\nc\n";
  const std::string yours = "a\nX\nc\n";
  LineMerge merge = merge_lines(base, yours, base);
  REQUIRE(merge.chunks.size() == 3);
  CHECK(merge.chunks[0].kind == ChunkKind::same);
  CHECK(merge.chunks[1].kind == ChunkKind::yours);
  CHECK(merge.chunks[2].kind == ChunkKind::same);
  CHECK(merge.conflicts() == 0);
  CHECK(merge_result(merge, default_picks(merge)) == yours);
}

TEST_CASE("line_merge: merge_lines other only change") {
  const std::string base = "a\nb\nc\n";
  const std::string other = "a\nX\nc\n";
  LineMerge merge = merge_lines(base, base, other);
  REQUIRE(merge.chunks.size() == 3);
  CHECK(merge.chunks[1].kind == ChunkKind::other);
  CHECK(merge.conflicts() == 0);
  CHECK(merge_result(merge, default_picks(merge)) == other);
}

TEST_CASE("line_merge: merge_lines both same change") {
  const std::string base = "a\nb\nc\n";
  const std::string changed = "a\nX\nc\n";
  LineMerge merge = merge_lines(base, changed, changed);
  REQUIRE(merge.chunks.size() == 3);
  CHECK(merge.chunks[1].kind == ChunkKind::both_same);
  CHECK(merge.conflicts() == 0);
  CHECK(merge_result(merge, default_picks(merge)) == changed);
}

TEST_CASE("line_merge: merge_lines conflict same line") {
  const std::string base = "a\nb\nc\n";
  const std::string yours = "a\nY\nc\n";
  const std::string other = "a\nZ\nc\n";
  LineMerge merge = merge_lines(base, yours, other);
  REQUIRE(merge.chunks.size() == 3);
  CHECK(merge.chunks[1].kind == ChunkKind::conflict);
  REQUIRE(merge.conflicts() == 1);
  CHECK(merge_result(merge, {Pick::open, Pick::yours, Pick::open}) == yours);
  CHECK(merge_result(merge, {Pick::open, Pick::other, Pick::open}) == other);
  CHECK(merge_result(merge, {Pick::open, Pick::both, Pick::open}) == "a\nY\nZ\nc\n");
}

TEST_CASE("line_merge: picking the unchanged side of a one-sided chunk leaves the change out") {
  const std::string base = "a\nb\nc\n";
  const std::string yours = "a\nX\nc\n";
  LineMerge merge = merge_lines(base, yours, base);
  REQUIRE(merge.chunks.size() == 3);
  CHECK(merge.chunks[1].kind == ChunkKind::yours);
  CHECK(merge_result(merge, {Pick::open, Pick::other, Pick::open}) == base);
  CHECK(merge_result(merge, {Pick::open, Pick::yours, Pick::open}) == yours);
}

TEST_CASE("line_merge: merge_lines conflict on adjacent lines") {
  const std::string base = "a\nb\nc\nd\ne\n";
  const std::string yours = "a\nb\nC\nd\ne\n";
  const std::string other = "a\nb\nc\nD\ne\n";
  LineMerge merge = merge_lines(base, yours, other);
  REQUIRE(merge.chunks.size() == 3);
  CHECK(merge.chunks[1].kind == ChunkKind::conflict);
  CHECK(merge.chunks[1].base_begin == 2);
  CHECK(merge.chunks[1].base_end == 4);
  REQUIRE(merge.conflicts() == 1);
}

TEST_CASE("line_merge: merge_lines inserts same place different text") {
  const std::string base = "a\nc\n";
  const std::string yours = "a\nX\nc\n";
  const std::string other = "a\nY\nc\n";
  LineMerge merge = merge_lines(base, yours, other);
  REQUIRE(merge.chunks.size() == 3);
  CHECK(merge.chunks[1].kind == ChunkKind::conflict);
}

TEST_CASE("line_merge: merge_lines inserts same place same text") {
  const std::string base = "a\nc\n";
  const std::string side = "a\nX\nc\n";
  LineMerge merge = merge_lines(base, side, side);
  REQUIRE(merge.chunks.size() == 3);
  CHECK(merge.chunks[1].kind == ChunkKind::both_same);
  CHECK(merge_result(merge, default_picks(merge)) == side);
}

TEST_CASE("line_merge: merge_lines delete vs change conflict") {
  const std::string base = "a\nb\nc\n";
  const std::string yours = "a\nc\n";
  const std::string other = "a\nB\nc\n";
  LineMerge merge = merge_lines(base, yours, other);
  REQUIRE(merge.chunks.size() == 3);
  CHECK(merge.chunks[1].kind == ChunkKind::conflict);
}

TEST_CASE("line_merge: merge_lines one empty side") {
  const std::string base = "a\nb\n";
  LineMerge merge = merge_lines(base, base, "");
  REQUIRE(merge.chunks.size() == 1);
  CHECK(merge.chunks[0].kind == ChunkKind::other);
  CHECK(merge.conflicts() == 0);
  CHECK(merge_result(merge, default_picks(merge)).empty());
}

TEST_CASE("line_merge: merge_lines all empty") {
  LineMerge merge = merge_lines("", "", "");
  CHECK(merge.chunks.empty());
  CHECK(merge.conflicts() == 0);
  CHECK(merge_result(merge, default_picks(merge)).empty());
}

TEST_CASE("line_merge: merge_result two conflicts") {
  const std::string base = "a\nb\nc\nd\ne\n";
  const std::string yours = "a\nB1\nc\nD1\ne\n";
  const std::string other = "a\nB2\nc\nD2\ne\n";
  LineMerge merge = merge_lines(base, yours, other);
  REQUIRE(merge.conflicts() == 2);
  CHECK(merge_result(merge, {Pick::open, Pick::yours, Pick::open, Pick::yours, Pick::open}) == "a\nB1\nc\nD1\ne\n");
  CHECK(merge_result(merge, {Pick::open, Pick::other, Pick::open, Pick::other, Pick::open}) == "a\nB2\nc\nD2\ne\n");
  CHECK(merge_result(merge, {Pick::open, Pick::both, Pick::open, Pick::both, Pick::open}) ==
        "a\nB1\nB2\nc\nD1\nD2\ne\n");
  CHECK(merge_result(merge, {Pick::open, Pick::yours, Pick::open, Pick::other, Pick::open}) == "a\nB1\nc\nD2\ne\n");
}

TEST_CASE("line_merge: merge_result no conflict") {
  const std::string base = "a\nb\nc\n";
  const std::string changed = "a\nX\nc\n";
  LineMerge merge = merge_lines(base, changed, changed);
  REQUIRE(merge.conflicts() == 0);
  CHECK(merge_result(merge, default_picks(merge)) == changed);
}

TEST_CASE("line_merge: cap over limit") {
  std::vector<std::string> base_lines = make_lines(1);
  std::vector<std::string> yours_lines = base_lines;
  for (int i = 0; i < 2001; ++i) yours_lines.insert(yours_lines.begin(), "new" + std::to_string(i) + "\n");
  const std::string base = join(base_lines);
  const std::string yours = join(yours_lines);
  LineMerge merge = merge_lines(base, yours, base);
  CHECK(merge.too_many);
  CHECK(merge.chunks.empty());
}

TEST_CASE("line_merge: cap at limit") {
  std::vector<std::string> base_lines = make_lines(1);
  std::vector<std::string> yours_lines = base_lines;
  for (int i = 0; i < 2000; ++i) yours_lines.insert(yours_lines.begin(), "new" + std::to_string(i) + "\n");
  const std::string base = join(base_lines);
  const std::string yours = join(yours_lines);
  LineMerge merge = merge_lines(base, yours, base);
  CHECK_FALSE(merge.too_many);
}

TEST_CASE("line_merge: property b-y-b, b-b-o, b-x-x, coverage") {
  const std::vector<std::string_view> pool{"p\n", "q\n", "r\n", "s\n"};
  std::mt19937 rng(12345);
  std::uniform_int_distribution<int> len_dist(0, 12);
  std::uniform_int_distribution<int> pool_dist(0, static_cast<int>(pool.size()) - 1);
  std::uniform_int_distribution<int> chop_dist(0, 3);  

  auto make_text = [&]() {
    const int len = len_dist(rng);
    std::string text;
    for (int i = 0; i < len; ++i) text += pool[static_cast<std::size_t>(pool_dist(rng))];
    if (!text.empty() && chop_dist(rng) == 0) text.pop_back();
    return text;
  };

  for (int i = 0; i < 2000; ++i) {
    const std::string b = make_text();
    const std::string y = make_text();
    const std::string o = make_text();

    LineMerge merge_byb = merge_lines(b, y, b);
    CHECK(merge_byb.conflicts() == 0);
    CHECK(merge_result(merge_byb, default_picks(merge_byb)) == y);

    LineMerge merge_bbo = merge_lines(b, b, o);
    CHECK(merge_bbo.conflicts() == 0);
    CHECK(merge_result(merge_bbo, default_picks(merge_bbo)) == o);

    LineMerge merge_bxx = merge_lines(b, y, y);
    CHECK(merge_bxx.conflicts() == 0);
    CHECK(merge_result(merge_bxx, default_picks(merge_bxx)) == y);

    LineMerge merge_byo = merge_lines(b, y, o);
    check_coverage(merge_byo);

    const std::vector<Pick> all_yours(merge_byo.chunks.size(), Pick::yours);
    CHECK(merge_result(merge_byo, all_yours) == y);
    const std::vector<Pick> all_other(merge_byo.chunks.size(), Pick::other);
    CHECK(merge_result(merge_byo, all_other) == o);
  }
}

TEST_CASE("line_merge: speed 50000 lines") {
  const int n = 50000;
  std::vector<std::string> base_lines = make_lines(n);
  std::vector<std::string> yours_lines = base_lines;
  std::vector<std::string> other_lines = base_lines;
  for (int i = 0; i < 10; ++i) {
    yours_lines[static_cast<std::size_t>(i * 4000 + 100)] = "yours" + std::to_string(i) + "\n";
    other_lines[static_cast<std::size_t>(i * 4000 + 2100)] = "other" + std::to_string(i) + "\n";
  }
  const std::string base = join(base_lines);
  const std::string yours = join(yours_lines);
  const std::string other = join(other_lines);

  const auto start = std::chrono::steady_clock::now();
  LineMerge merge = merge_lines(base, yours, other);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  CHECK_FALSE(merge.too_many);
  CHECK(std::chrono::duration<double>(elapsed).count() < 0.5);
}
