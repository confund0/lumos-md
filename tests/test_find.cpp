#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "lumos/commands.hpp"
#include "lumos/document.hpp"
#include "lumos/syntax.hpp"

using namespace lumos;

namespace {

const std::string sharp_s_cap = "\xe1\xba\x9e";

void apply(Document& doc, const CommandResult& r) {
  doc.begin();
  for (const Edit& e : r.edits) REQUIRE_NOTHROW(doc.apply(e));
  doc.commit();
}

}  

TEST_CASE("find: find_all finds a literal match", "[find]") {
  Document doc("abc abc");
  const auto r = find_all(doc, "abc", {});
  REQUIRE(r.has_value());
  REQUIRE(r->size() == 2);
  CHECK((*r)[0] == ByteRange{0, 3});
  CHECK((*r)[1] == ByteRange{4, 7});
}

TEST_CASE("find: find_all finds multiple non-overlapping matches", "[find]") {
  Document doc("aaaa");
  const auto r = find_all(doc, "aa", {});
  REQUIRE(r.has_value());
  REQUIRE(r->size() == 2);
  CHECK((*r)[0] == ByteRange{0, 2});
  CHECK((*r)[1] == ByteRange{2, 4});
}

TEST_CASE("find: find_all is case-insensitive by default", "[find]") {
  Document doc("ABC abc");
  const auto r = find_all(doc, "abc", {});
  REQUIRE(r.has_value());
  CHECK(r->size() == 2);
}

TEST_CASE("find: find_all case_sensitive true skips a different-case match", "[find]") {
  Document doc("ABC abc");
  FindOptions opt;
  opt.case_sensitive = true;
  const auto r = find_all(doc, "abc", opt);
  REQUIRE(r.has_value());
  REQUIRE(r->size() == 1);
  CHECK((*r)[0] == ByteRange{4, 7});
}

TEST_CASE("find: find_all case-insensitive folds a multi-byte character", "[find]") {
  Document doc("a" + sharp_s_cap + "b");
  const auto r = find_all(doc, "ss", {});
  REQUIRE(r.has_value());
  REQUIRE(r->size() == 1);
  CHECK((*r)[0] == ByteRange{1, 1 + sharp_s_cap.size()});
}

TEST_CASE("find: case-insensitive fold search does not match half of a folded character", "[find]") {
  Document doc("a" + sharp_s_cap + "b");
  const auto r = find_all(doc, "s", {});
  REQUIRE(r.has_value());
  CHECK(r->empty());
}

TEST_CASE("find: whole_word excludes a match inside a longer word", "[find]") {
  Document doc("cathedral cat");
  FindOptions opt;
  opt.whole_word = true;
  const auto r = find_all(doc, "cat", opt);
  REQUIRE(r.has_value());
  REQUIRE(r->size() == 1);
  CHECK((*r)[0] == ByteRange{10, 13});
}

TEST_CASE("find: whole_word allows a match at punctuation or a document edge", "[find]") {
  Document doc("cat, cat.cat");
  FindOptions opt;
  opt.whole_word = true;
  const auto r = find_all(doc, "cat", opt);
  REQUIRE(r.has_value());
  REQUIRE(r->size() == 3);
  CHECK((*r)[0] == ByteRange{0, 3});
  CHECK((*r)[1] == ByteRange{5, 8});
  CHECK((*r)[2] == ByteRange{9, 12});
}

TEST_CASE("find: find_all with an empty pattern returns no matches", "[find]") {
  Document doc("abc");
  const auto r = find_all(doc, "", {});
  REQUIRE(r.has_value());
  CHECK(r->empty());
}

TEST_CASE("find: find_all with an invalid regex returns nullopt", "[find]") {
  Document doc("abc");
  FindOptions opt;
  opt.regex = true;
  const auto r = find_all(doc, "(", opt);
  CHECK_FALSE(r.has_value());
}

TEST_CASE("find: regex zero-width match does not loop forever", "[find]") {
  Document doc("bbb");
  FindOptions opt;
  opt.regex = true;
  const auto r = find_all(doc, "a*", opt);
  REQUIRE(r.has_value());
  CHECK(r->size() == 4);  
}

TEST_CASE("find: regex . matches one byte, not one codepoint (accepted open item)", "[find]") {
  Document doc("\xc3\xa9");  
  FindOptions opt;
  opt.regex = true;
  const auto r = find_all(doc, ".", opt);
  REQUIRE(r.has_value());
  CHECK(r->size() == 2);  
}

TEST_CASE("find: replace_one replaces a literal match", "[find]") {
  Document doc("foo bar");
  const auto m = find_all(doc, "bar", {});
  REQUIRE(m.has_value());
  REQUIRE(m->size() == 1);
  const auto r = replace_one(doc, (*m)[0], "bar", "baz", {});
  REQUIRE(r.has_value());
  apply(doc, *r);
  CHECK(doc.text() == "foo baz");
  CHECK(r->selection == Selection{7, 7});
}

TEST_CASE("find: replace_one expands capture groups in regex mode", "[find]") {
  Document doc("ab");
  FindOptions opt;
  opt.regex = true;
  const auto r = replace_one(doc, ByteRange{0, 2}, "(a)(b)", "$2$1", opt);
  REQUIRE(r.has_value());
  apply(doc, *r);
  CHECK(doc.text() == "ba");
}

TEST_CASE("find: replace_one returns nullopt when the match no longer matches pattern", "[find]") {
  Document doc("xyz");
  const auto r = replace_one(doc, ByteRange{0, 1}, "a", "Z", {});
  CHECK_FALSE(r.has_value());
}

TEST_CASE("find: replace_all applies every match as one undo step", "[find]") {
  Document doc("a a a");
  const auto r = replace_all(doc, "a", "bb", {});
  REQUIRE(r.has_value());
  apply(doc, *r);
  CHECK(doc.text() == "bb bb bb");
  doc.undo();
  CHECK(doc.text() == "a a a");
}

TEST_CASE("find: replace_all keeps the untouched text byte-identical", "[find]") {
  Document doc("XXXaYYYaZZZ");
  const auto r = replace_all(doc, "a", "Q", {});
  REQUIRE(r.has_value());
  apply(doc, *r);
  CHECK(doc.text() == "XXXQYYYQZZZ");
  CHECK(doc.text().substr(0, 3) == "XXX");
  CHECK(doc.text().substr(doc.text().size() - 3) == "ZZZ");
}

TEST_CASE("find: replace_all expands capture groups in every match", "[find]") {
  Document doc("ab ab");
  FindOptions opt;
  opt.regex = true;
  const auto r = replace_all(doc, "(a)(b)", "$2$1", opt);
  REQUIRE(r.has_value());
  apply(doc, *r);
  CHECK(doc.text() == "ba ba");
}

TEST_CASE("find: replace_all returns nullopt when there is no match", "[find]") {
  Document doc("xyz");
  const auto r = replace_all(doc, "q", "z", {});
  CHECK_FALSE(r.has_value());
}

TEST_CASE("find: replace_all returns nullopt for an invalid regex", "[find]") {
  Document doc("xyz");
  FindOptions opt;
  opt.regex = true;
  const auto r = replace_all(doc, "(", "z", opt);
  CHECK_FALSE(r.has_value());
}

TEST_CASE("commands: property check of find and replace over random documents", "[random]") {
  std::mt19937 rng(42);
  const std::vector<std::string> words = {"cat", "dog", "the", "and", "foo", "bar", "a", "test"};
  int applied = 0;
  for (int round = 0; round < 3000; ++round) {
    std::string text;
    const int n = std::uniform_int_distribution<int>(0, 20)(rng);
    for (int i = 0; i < n; ++i) {
      text += words[std::uniform_int_distribution<std::size_t>(0, words.size() - 1)(rng)];
      text += std::uniform_int_distribution<int>(0, 1)(rng) ? ' ' : '\n';
    }
    std::string pattern = words[std::uniform_int_distribution<std::size_t>(0, words.size() - 1)(rng)];
    FindOptions opt;
    opt.case_sensitive = std::uniform_int_distribution<int>(0, 1)(rng) != 0;
    opt.whole_word = std::uniform_int_distribution<int>(0, 1)(rng) != 0;
    opt.regex = std::uniform_int_distribution<int>(0, 3)(rng) == 0;
    if (opt.regex) pattern += std::uniform_int_distribution<int>(0, 1)(rng) ? "+" : "*";

    Document doc(text);
    const auto all = find_all(doc, pattern, opt);
    if (!all) continue;  
    for (const ByteRange& m : *all) {
      REQUIRE(m.start <= m.end);
      REQUIRE(m.end <= text.size());
    }
    const auto r = replace_all(doc, pattern, "X", opt);
    if (!r) continue;  
    INFO("text: " << text << " pattern: " << pattern);
    doc.begin();
    for (const Edit& e : r->edits) REQUIRE_NOTHROW(doc.apply(e));
    doc.commit();
    REQUIRE(r->selection.cursor <= doc.size());
    REQUIRE(r->selection.anchor <= doc.size());
    REQUIRE_FALSE(doc.tree().validate(doc.size()).has_value());
    doc.undo();
    REQUIRE(doc.text() == text);
    ++applied;
  }
  CHECK(applied > 300);
}
