#include <catch2/catch_test_macros.hpp>
#include <pthread.h>

#include <chrono>
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

TEST_CASE("find: regex . matches one codepoint", "[find]") {
  Document doc("\xc3\xa9");  
  FindOptions opt;
  opt.regex = true;
  const auto r = find_all(doc, ".", opt);
  REQUIRE(r.has_value());
  CHECK(*r == std::vector<ByteRange>{{0, 2}});
}

TEST_CASE("find: case-insensitive regex folds Unicode case", "[find]") {
  Document doc("\xc3\x89t\xc3\xa9");  
  FindOptions opt;
  opt.regex = true;
  const auto r = find_all(doc, "\xc3\xa9", opt);
  REQUIRE(r.has_value());
  CHECK(*r == std::vector<ByteRange>{{0, 2}, {3, 5}});
}

TEST_CASE("find: regex skips bytes that are not valid UTF-8", "[find]") {
  Document doc("a\xff" "b\xc3");
  FindOptions opt;
  opt.regex = true;
  const auto dot = find_all(doc, ".", opt);
  REQUIRE(dot.has_value());
  CHECK(*dot == std::vector<ByteRange>{{0, 1}, {2, 3}});
  CHECK(find_all(doc, "x*", opt).has_value());
}

TEST_CASE("find: regex sees a bad byte as the edge of the text", "[find]") {
  Document doc("a\xff" "b");
  FindOptions opt;
  opt.regex = true;
  const auto r = find_all(doc, "^b", opt);
  REQUIRE(r.has_value());
  CHECK(*r == std::vector<ByteRange>{{2, 3}});
  CHECK(replace_one(doc, ByteRange{2, 3}, "^b", "X", opt).has_value());
  CHECK_FALSE(replace_one(doc, ByteRange{0, 3}, "a.b", "X", opt).has_value());
}

TEST_CASE("find: regex with one hit per character on a 1 MiB line is linear", "[find]") {
  const std::size_t size = 1024 * 1024;
  const Document doc(std::string(size, 'a'));
  FindOptions opt;
  opt.regex = true;
  const auto start = std::chrono::steady_clock::now();
  const auto r = find_all(doc, ".", opt);
  const auto took = std::chrono::steady_clock::now() - start;
  REQUIRE(r.has_value());
  CHECK(r->size() == size);
  CHECK(took < std::chrono::seconds(5));
}

TEST_CASE("find: regex replace_all over 2,000 matches in 900 KB is linear", "[find]") {
  const std::string line =
      "1. Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et "
      "dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex "
      "ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat "
      "nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit "
      "anim id est laborum.\n";
  std::string text;
  for (int i = 0; i < 2000; ++i) text += line;
  const Document doc(text);
  FindOptions opt;
  opt.regex = true;
  const auto start = std::chrono::steady_clock::now();
  const auto r = replace_all(doc, "dolor\\ssit\\s(amet)", "x $1", opt);
  const auto took = std::chrono::steady_clock::now() - start;
  REQUIRE(r.has_value());
  CHECK(r->edits.size() == 2000);
  CHECK(r->edits.front().inserted == "x amet");
  CHECK(took < std::chrono::seconds(2));
}

TEST_CASE("find: regex leaves out an empty match right after a match, as sed", "[find]") {
  FindOptions opt;
  opt.regex = true;
  const auto replaced = [&](const char* text) {
    Document doc(text);
    const auto r = replace_all(doc, "a*", "x", opt);
    REQUIRE(r.has_value());
    apply(doc, *r);
    return doc.text();
  };
  CHECK(replaced("baaaac") == "xbxcx");
  CHECK(replaced("ab") == "xbx");
  CHECK(replaced("bbb") == "xbxbxbx");
  CHECK(replaced("aaa") == "x");
}

TEST_CASE("find: each line is its own regex subject, as sed", "[find]") {
  Document doc("1. a\n2. b\r\n\n3. c\n");
  FindOptions opt;
  opt.regex = true;
  const auto starts = find_all(doc, "^\\d", opt);
  REQUIRE(starts.has_value());
  CHECK(*starts == std::vector<ByteRange>{{0, 1}, {5, 6}, {12, 13}});
  const auto ends = find_all(doc, "\\w$", opt);
  REQUIRE(ends.has_value());
  CHECK(*ends == std::vector<ByteRange>{{3, 4}, {8, 9}, {15, 16}});
  const auto runs = find_all(doc, "[^x]+", opt);
  REQUIRE(runs.has_value());
  CHECK(*runs == std::vector<ByteRange>{{0, 4}, {5, 9}, {12, 16}});
  const auto spaces = find_all(doc, "a\\s+2", opt);
  REQUIRE(spaces.has_value());
  CHECK(spaces->empty());
  const auto empty = find_all(doc, "^$", opt);
  REQUIRE(empty.has_value());
  CHECK(*empty == std::vector<ByteRange>{{11, 11}});
  CHECK(replace_one(doc, ByteRange{12, 13}, "^\\d", "X", opt).has_value());
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

TEST_CASE("find: replacement expands $&, $$, two-digit groups and unset groups", "[find]") {
  Document doc("abcdefghijk");
  FindOptions opt;
  opt.regex = true;
  const auto r = replace_one(doc, ByteRange{0, 11}, "(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)(x)?", "[$11|$1|$&|$$|$12|$x]",
                             opt);
  REQUIRE(r.has_value());
  apply(doc, *r);
  CHECK(doc.text() == "[k|a|abcdefghijk|$||$x]");
}

TEST_CASE("find: replace_one matches with the text before the match in view", "[find]") {
  Document doc("ab b");
  FindOptions opt;
  opt.regex = true;
  const auto m = find_all(doc, "(?<=a)b", opt);
  REQUIRE(m.has_value());
  REQUIRE(*m == std::vector<ByteRange>{{1, 2}});
  CHECK(replace_one(doc, (*m)[0], "(?<=a)b", "X", opt).has_value());
  CHECK_FALSE(replace_one(doc, ByteRange{3, 4}, "(?<=a)b", "X", opt).has_value());
}

TEST_CASE("find: replace_one takes the non-empty match after an empty one", "[find]") {
  Document doc("ab");
  FindOptions opt;
  opt.regex = true;
  const auto m = find_all(doc, "(?=b)|b", opt);
  REQUIRE(m.has_value());
  REQUIRE(*m == std::vector<ByteRange>{{1, 1}, {1, 2}});
  CHECK(replace_one(doc, ByteRange{1, 1}, "(?=b)|b", "X", opt).has_value());
  CHECK(replace_one(doc, ByteRange{1, 2}, "(?=b)|b", "X", opt).has_value());
  const auto r = replace_all(doc, "(?=b)|b", "X", opt);
  REQUIRE(r.has_value());
  apply(doc, *r);
  CHECK(doc.text() == "aXX");
}

namespace {

void run_on_small_stack(void (*fn)(void*), void* arg) {
  struct Call {
    void (*fn)(void*);
    void* arg;
  } call{fn, arg};
  pthread_attr_t attr;
  REQUIRE(pthread_attr_init(&attr) == 0);
  REQUIRE(pthread_attr_setstacksize(&attr, 128 * 1024) == 0);
  pthread_t t;
  const int rc = pthread_create(
      &t, &attr,
      [](void* p) -> void* {
        auto* c = static_cast<Call*>(p);
        c->fn(c->arg);
        return nullptr;
      },
      &call);
  pthread_attr_destroy(&attr);
  REQUIRE(rc == 0);
  REQUIRE(pthread_join(t, nullptr) == 0);
}

struct LongLineRun {
  const Document* doc;
  std::optional<std::vector<ByteRange>> any;      
  std::optional<std::vector<ByteRange>> none;     
  std::optional<std::vector<ByteRange>> backtrack;  
  std::optional<std::vector<ByteRange>> group;    
  std::optional<CommandResult> replaced;          
  std::optional<CommandResult> replaced_none;     
};

void long_line_run(void* p) {
  auto* run = static_cast<LongLineRun*>(p);
  FindOptions opt;
  opt.regex = true;
  run->any = find_all(*run->doc, "[^x]*", opt);
  run->none = find_all(*run->doc, "(a|b)*c", opt);
  run->backtrack = find_all(*run->doc, "(a*)*[bc]", opt);
  run->group = find_all(*run->doc, "(a|b)*", opt);
  run->replaced = replace_all(*run->doc, "[^x]*", "y", opt);
  run->replaced_none = replace_all(*run->doc, "(a|b)*c", "y", opt);
}

void check_long_line_run(const LongLineRun& run, std::size_t size) {
  REQUIRE(run.any.has_value());
  CHECK(*run.any == std::vector<ByteRange>{{0, size}});
  REQUIRE(run.none.has_value());
  CHECK(run.none->empty());
  CHECK_FALSE(run.backtrack.has_value());  
  CHECK_FALSE(run.group.has_value());      
  REQUIRE(run.replaced.has_value());
  CHECK(run.replaced->edits.size() == 1);
  CHECK_FALSE(run.replaced_none.has_value());
}

}  

TEST_CASE("find: regex over a 4 MiB line on the main thread and a 128 KiB stack", "[find]") {
  const std::size_t size = 4 * 1024 * 1024;
  const Document doc(std::string(size, 'a'));
  LongLineRun main_run{&doc, {}, {}, {}, {}, {}, {}};
  long_line_run(&main_run);
  check_long_line_run(main_run, size);
  LongLineRun thread_run{&doc, {}, {}, {}, {}, {}, {}};
  run_on_small_stack(long_line_run, &thread_run);
  check_long_line_run(thread_run, size);
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
