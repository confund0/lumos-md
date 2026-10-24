
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "md/content.hpp"
#include "md/link_syntax.hpp"
#include "md/ref_index.hpp"

namespace md = lumos::md;

TEST_CASE("md refs: scan_link_label") {
  CHECK(md::scan_link_label("[a]", 0) == 3);
  CHECK(md::scan_link_label("x[a]", 1) == 4);
  CHECK_FALSE(md::scan_link_label("a]", 0).has_value());  
  CHECK_FALSE(md::scan_link_label("[a", 0).has_value());  
  CHECK_FALSE(md::scan_link_label("[a[b]", 0).has_value());  
  CHECK(md::scan_link_label("[a\\]b]", 0) == 6);  
  CHECK(md::scan_link_label("[a\\\\]", 0) == 5);  
  CHECK_FALSE(md::scan_link_label("[ ]", 0).has_value());
  CHECK_FALSE(md::scan_link_label("[ \t\n ]", 0).has_value());
  CHECK(md::scan_link_label("[\\ ]", 0) == 4);  

  const std::string taken = "[" + std::string(1000, 'a') + "]";
  CHECK(md::scan_link_label(taken, 0) == taken.size());
  const std::string refused = "[" + std::string(1001, 'a') + "]";
  CHECK_FALSE(md::scan_link_label(refused, 0).has_value());
  const std::string with_nul = "[a" + std::string(1, '\0') + "b]";
  CHECK(md::scan_link_label(with_nul, 0) == with_nul.size());
}

TEST_CASE("md refs: scan_link_destination, plain") {
  const auto d = md::scan_link_destination("/url x", 0);
  REQUIRE(d.has_value());
  CHECK(d->start == 0);
  CHECK(d->end == 4);
  CHECK_FALSE(d->pointy);
  CHECK_FALSE(md::scan_link_destination(" /url", 0).has_value());  
  CHECK(md::scan_link_destination("(a)b x", 0)->end == 4);  
  CHECK_FALSE(md::scan_link_destination("(a x", 0).has_value());  
  CHECK(md::scan_link_destination("a\\)b x", 0)->end == 4);  
  CHECK(md::scan_link_destination("a\x01 b", 0)->end == 2);  
  CHECK(md::scan_link_destination("/url", 0)->end == 4);
}

TEST_CASE("md refs: scan_link_destination, pointy") {
  const auto d = md::scan_link_destination("<a b>x", 0);
  REQUIRE(d.has_value());
  CHECK(d->start == 0);
  CHECK(d->end == 5);
  CHECK(d->pointy);
  CHECK(md::scan_link_destination("<>", 0)->end == 2);  
  CHECK_FALSE(md::scan_link_destination("<a", 0).has_value());  
  CHECK_FALSE(md::scan_link_destination("<a\nb>", 0).has_value());  
  CHECK_FALSE(md::scan_link_destination("<a<b>", 0).has_value());  
  CHECK(md::scan_link_destination("<a\\>b>", 0)->end == 6);  
  CHECK_FALSE(md::scan_link_destination("<a\\\n>", 0).has_value());  
}

TEST_CASE("md refs: scan_link_title") {
  CHECK(md::scan_link_title("\"t\"x", 0) == 3);
  CHECK(md::scan_link_title("'t'x", 0) == 3);
  CHECK(md::scan_link_title("(t)x", 0) == 3);
  CHECK(md::scan_link_title("\"a\\\"b\"x", 0) == 6);  
  CHECK(md::scan_link_title("\"a\\nb\"x", 0) == 6);  
  CHECK_FALSE(md::scan_link_title("\"a", 0).has_value());  
  CHECK_FALSE(md::scan_link_title("(a(b)c)", 0).has_value());  
  CHECK(md::scan_link_title("(a\\(b)c)", 0) == 6);  
  CHECK_FALSE(md::scan_link_title("x", 0).has_value());  
  CHECK(md::scan_link_title("\"a\nb\"x", 0) == 5);  
}

TEST_CASE("md refs: skip_link_whitespace") {
  bool saw = false;
  CHECK(md::skip_link_whitespace("  x", 0, false, &saw) == 2);
  CHECK_FALSE(saw);  
  CHECK(md::skip_link_whitespace("  \n  x", 0, false, &saw) == 5);
  CHECK(saw);
  CHECK(md::skip_link_whitespace("\r\n x", 0, false, &saw) == 3);
  CHECK(saw);
  CHECK(md::skip_link_whitespace("\v x", 0, false, &saw) == 0);
  CHECK(md::skip_link_whitespace("  ", 0, false, &saw) == 2);
  CHECK(saw);
}

TEST_CASE("md refs: skip_link_whitespace, full (inline links)") {
  bool saw = false;
  CHECK(md::skip_link_whitespace("\v\f x", 0, true, &saw) == 3);
  CHECK_FALSE(saw);
  CHECK(md::skip_link_whitespace(" \n\n x", 0, true, &saw) == 4);
  CHECK(saw);
  CHECK(md::skip_link_whitespace("x", 0, true, &saw) == 0);
}

namespace {

md::DefinitionSyntax must_scan(std::string_view s) {
  const auto d = md::scan_link_reference_definition(s);
  REQUIRE(d.has_value());
  return *d;
}

}  

TEST_CASE("md refs: scan_link_reference_definition") {
  SECTION("label, colon, destination only") {
    const auto d = must_scan("[a]: /u\n");
    CHECK(std::string(std::string_view("[a]: /u\n").substr(d.label_start, d.label_end - d.label_start)) == "[a]");
    CHECK(d.destination.start == 5);
    CHECK(d.destination.end == 7);
    CHECK(d.title_start == d.title_end);
    CHECK(d.end == 8);
  }
  SECTION("pointy destination and a title") {
    const std::string s = "[a]: <u r> 'ti'\n";
    const auto d = must_scan(s);
    CHECK(d.destination.pointy);
    CHECK(s.substr(d.destination.start, d.destination.end - d.destination.start) == "<u r>");
    CHECK(s.substr(d.title_start, d.title_end - d.title_start) == "'ti'");
    CHECK(d.end == s.size());
  }
  SECTION("title with trailing garbage falls back to no title") {
    const std::string s = "[a]: /u\n\"t\" x";
    const auto d = must_scan(s);
    CHECK(d.title_start == d.title_end);
    CHECK(d.end == 8);  
  }
  SECTION("no destination: not a definition") {
    CHECK_FALSE(md::scan_link_reference_definition("[a]:\n").has_value());
  }
  SECTION("no colon: not a definition") { CHECK_FALSE(md::scan_link_reference_definition("[a] /u\n").has_value()); }
  SECTION("no line ending at the true end: still complete") {
    const auto d = must_scan("[a]: /u");
    CHECK(d.end == 7);
  }
}

TEST_CASE("md refs: normalize_label, case fold") {
  CHECK(md::normalize_label("ẞ") == md::normalize_label("ss"));
  CHECK(md::normalize_label("ABC") == "abc");
}

TEST_CASE("md refs: normalize_label, whitespace") {
  CHECK(md::normalize_label("  a   b  ") == "a b");
  CHECK(md::normalize_label("a\vb") == "a b");
  CHECK(md::normalize_label("a\n\t\fb") == "a b");
  CHECK(md::normalize_label("a\\!b") == "a\\!b");
  CHECK(md::normalize_label("   ") == "");
}

TEST_CASE("md refs: normalize_label, invalid UTF-8 and U+0000") {
  const std::string invalid = "a\xFF" "b";
  const std::string expected_ffFD = "a\xEF\xBF\xBD" "b";
  CHECK(md::normalize_label(invalid) == expected_ffFD);
  const std::string with_nul = std::string("a") + '\0' + "b";
  CHECK(md::normalize_label(with_nul) == expected_ffFD);
}

TEST_CASE("md refs: RefIndex intern, find, label") {
  md::RefIndex idx;
  const md::LabelId a = idx.intern("foo");
  const md::LabelId b = idx.intern("bar");
  CHECK(idx.intern("foo") == a);  
  CHECK(a != b);
  md::LabelId out = 0;
  CHECK(idx.find("foo", out));
  CHECK(out == a);
  CHECK_FALSE(idx.find("baz", out));
  CHECK(idx.label(a) == "foo");
  CHECK(idx.label(b) == "bar");
  CHECK_FALSE(idx.is_defined(a));  
}

TEST_CASE("md refs: RefIndex::replace_definitions flips") {
  md::RefIndex idx;
  const md::LabelId a = idx.intern("a");
  const md::LabelId b = idx.intern("b");

  std::vector<md::LabelId> flipped;
  idx.replace_definitions(0, 0, 10, std::vector<md::RefIndex::Definition>{{0, a}}, flipped);
  CHECK(flipped == std::vector<md::LabelId>{a});
  CHECK(idx.is_defined(a));
  CHECK_FALSE(idx.is_defined(b));

  flipped.clear();
  idx.replace_definitions(10, 10, 20, std::vector<md::RefIndex::Definition>{{10, b}}, flipped);
  CHECK(flipped == std::vector<md::LabelId>{b});
  CHECK(idx.is_defined(a));
  CHECK(idx.is_defined(b));

  flipped.clear();
  idx.replace_definitions(0, 10, 0, std::span<const md::RefIndex::Definition>{}, flipped);
  CHECK(flipped == std::vector<md::LabelId>{a});
  CHECK_FALSE(idx.is_defined(a));
  CHECK(idx.is_defined(b));

  flipped.clear();
  idx.replace_definitions(0, 10, 20, std::vector<md::RefIndex::Definition>{{0, b}, {10, a}}, flipped);
  CHECK(flipped == std::vector<md::LabelId>{a});
  CHECK(idx.is_defined(a));
  CHECK(idx.is_defined(b));
}

TEST_CASE("md refs: RefIndex::definitions and definitions_in") {
  md::RefIndex idx;
  const md::LabelId a = idx.intern("a");
  const md::LabelId b = idx.intern("b");
  std::vector<md::LabelId> flipped;
  idx.replace_definitions(0, 0, 20, std::vector<md::RefIndex::Definition>{{0, a}, {10, b}}, flipped);

  const auto all = idx.definitions();
  REQUIRE(all.size() == 2);
  CHECK(all[0].start == 0);
  CHECK(all[1].start == 10);

  std::vector<md::RefIndex::Definition> in_range;
  idx.definitions_in(5, 20, 100, in_range);
  REQUIRE(in_range.size() == 1);
  CHECK(in_range[0].start == 110);
  CHECK(in_range[0].label == b);

  std::vector<md::RefIndex::Definition> none;
  idx.definitions_in(20, 30, 0, none);
  CHECK(none.empty());
}

TEST_CASE("md refs: RefIndex::clear") {
  md::RefIndex idx;
  const md::LabelId a = idx.intern("a");
  std::vector<md::LabelId> flipped;
  idx.replace_definitions(0, 0, 5, std::vector<md::RefIndex::Definition>{{0, a}}, flipped);
  CHECK(idx.is_defined(a));
  idx.clear();
  md::LabelId out = 0;
  CHECK_FALSE(idx.find("a", out));
}

TEST_CASE("md refs: ContentBuffer segments and mapping") {
  md::ContentBuffer buf;
  buf.append(10, "abc");   
  buf.append(20, "defg");  
  CHECK(buf.text() == "abcdefg");
  CHECK(buf.source_start(1) == 11);
  CHECK(buf.source_end(1) == 11);
  CHECK(buf.source_end(3) == 13);
  CHECK(buf.source_start(3) == 20);
  CHECK(buf.source_end(7) == 24);

  std::vector<md::Segment> gaps;
  buf.gaps(gaps);
  REQUIRE(gaps.size() == 1);
  CHECK(gaps[0].start == 13);
  CHECK(gaps[0].end == 20);
}

TEST_CASE("md refs: ContentBuffer mapping with a segment hint") {
  md::ContentBuffer buf;
  for (std::size_t k = 0; k < 9; ++k) buf.append(k * 5, "xyz");
  for (std::size_t b = 0; b <= 27; ++b) {
    for (std::size_t hint = 0; hint <= 10; ++hint) {
      std::size_t seg = hint;
      CHECK(buf.source_start(b, seg) == buf.source_start(b));
      CHECK(seg == std::min<std::size_t>(b / 3, 8));
      seg = hint;
      CHECK(buf.source_end(b, seg) == buf.source_end(b));
      CHECK(seg == (b == 0 ? 0 : (b - 1) / 3));
    }
  }
  CHECK(buf.source_start(3) == 5);
  CHECK(buf.source_end(3) == 3);
}

TEST_CASE("md refs: ContentBuffer trim_end") {
  md::ContentBuffer buf;
  buf.append(0, "ab  ");
  buf.append(10, "cd\t ");
  buf.trim_end();
  CHECK(buf.text() == "ab  cd");
  md::ContentBuffer buf2;
  buf2.append(0, "ab");
  buf2.append(10, "   ");
  buf2.trim_end();
  CHECK(buf2.text() == "ab");
  CHECK(buf2.segments().size() == 1);
}
