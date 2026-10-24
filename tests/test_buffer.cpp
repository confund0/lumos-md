#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "lumos/buffer.hpp"

using lumos::Buffer;
using lumos::Edit;

namespace {

std::string read_by_chunks(const Buffer& b) {
  std::string out;
  std::size_t pos = 0;
  while (pos < b.size()) {
    const std::string_view chunk = b.chunk_at(pos);
    REQUIRE(!chunk.empty());
    out += chunk;
    pos += chunk.size();
  }
  CHECK(b.chunk_at(b.size()).empty());
  return out;
}

void apply_model(std::string& model, const Edit& e) { model.replace(e.offset, e.removed, e.inserted); }

bool is_continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0U) == 0x80U; }

}  

TEST_CASE("buffer round-trip keeps bytes as they are") {
  const std::vector<std::string> texts = {
      "",
      "# Title\r\n\r\nLine with CRLF\r\n",
      "# Title\n\nLine with LF\n",
      "mixed\r\nends\nhere\r\n",
      "\tTabbed\n    spaces\ttab \n",
      "trailing spaces   \nand tabs\t\t\n",
      "no final newline",
      "Příliš žluťoučký kůň úpěl ďábelské ódy\n",
      "emoji 😀🎉👍🏽 and ZWJ 👨‍👩‍👧\n",
      "CJK 漢字かなカナ한국어\n",
      "lone \r carriage return",
  };
  for (const std::string& t : texts) {
    const Buffer b(t);
    CHECK(b.size() == t.size());
    CHECK(b.text() == t);
    CHECK(read_by_chunks(b) == t);
    CHECK(b.slice(0, t.size()) == t);
    CHECK(b.version() == 0);
    CHECK_FALSE(b.can_undo());
    CHECK_FALSE(b.can_redo());
  }
}

TEST_CASE("buffer insert, delete and replace at start, middle and end") {
  const std::string base = "Hello world";

  SECTION("insert") {
    Buffer b(base);
    b.apply({0, 0, ">"});
    CHECK(b.text() == ">Hello world");
    b.apply({6, 0, ","});
    CHECK(b.text() == ">Hello, world");
    b.apply({b.size(), 0, "!"});
    CHECK(b.text() == ">Hello, world!");
  }
  SECTION("delete") {
    Buffer b(base);
    b.apply({0, 1, ""});
    CHECK(b.text() == "ello world");
    b.apply({4, 1, ""});
    CHECK(b.text() == "elloworld");
    b.apply({b.size() - 1, 1, ""});
    CHECK(b.text() == "elloworl");
    b.apply({0, b.size(), ""});
    CHECK(b.text().empty());
    CHECK(b.size() == 0);
  }
  SECTION("replace") {
    Buffer b(base);
    b.apply({0, 5, "Ahoj"});
    CHECK(b.text() == "Ahoj world");
    b.apply({4, 1, "_"});
    CHECK(b.text() == "Ahoj_world");
    b.apply({5, 5, "světe"});
    CHECK(b.text() == "Ahoj_světe");
  }
  SECTION("apply returns the inverse") {
    Buffer b(base);
    const Edit inv = b.apply({6, 5, "there"});
    CHECK(inv.offset == 6);
    CHECK(inv.removed == 5);
    CHECK(inv.inserted == "world");
    b.apply(inv);
    CHECK(b.text() == base);
  }
  SECTION("empty edit changes nothing") {
    Buffer b(base);
    b.apply({3, 0, ""});
    CHECK(b.version() == 0);
    CHECK_FALSE(b.can_undo());
  }
}

TEST_CASE("buffer edits across piece boundaries") {
  Buffer b("0123456789");
  b.apply({3, 0, "abc"});   
  b.apply({8, 0, "XYZ"});   
  b.apply({0, 0, "<"});     
  CHECK(b.text() == "<012abc34XYZ56789");
  CHECK(read_by_chunks(b) == b.text());

  CHECK(b.slice(2, 10) == "12abc34XYZ");
  CHECK(b.at(4) == 'a');
  CHECK(b.at(11) == 'Z');

  const Edit inv = b.apply({5, 7, "-"});  
  CHECK(inv.inserted == "bc34XYZ");
  CHECK(b.text() == "<012a-56789");
  b.apply({0, b.size(), "all new"});
  CHECK(b.text() == "all new");
  CHECK(read_by_chunks(b) == "all new");
}

TEST_CASE("buffer typing extends one piece") {
  Buffer b("ab");
  b.apply({1, 0, "x"});
  b.apply({2, 0, "y"});
  b.apply({3, 0, "z"});
  CHECK(b.text() == "axyzb");
  CHECK(b.chunk_at(1) == "xyz");
}

TEST_CASE("buffer undo and redo single edits") {
  Buffer b("abc");
  CHECK(b.undo().empty());
  CHECK(b.redo().empty());

  b.apply({3, 0, "d"});
  b.apply({0, 1, "A"});
  CHECK(b.text() == "Abcd");
  CHECK(b.version() == 2);

  std::vector<Edit> u = b.undo();
  REQUIRE(u.size() == 1);
  CHECK(u[0].offset == 0);
  CHECK(u[0].removed == 1);
  CHECK(u[0].inserted == "a");
  CHECK(b.text() == "abcd");
  CHECK(b.version() == 3);

  b.undo();
  CHECK(b.text() == "abc");
  CHECK_FALSE(b.can_undo());
  CHECK(b.can_redo());

  std::vector<Edit> r = b.redo();
  REQUIRE(r.size() == 1);
  CHECK(r[0].inserted == "d");
  CHECK(b.text() == "abcd");
  b.redo();
  CHECK(b.text() == "Abcd");
  CHECK_FALSE(b.can_redo());
  CHECK(b.version() == 6);
}

TEST_CASE("buffer transactions are one undo step") {
  Buffer b("word");
  b.begin();
  b.apply({0, 0, "**"});
  b.begin();  
  b.apply({6, 0, "**"});
  b.commit();
  CHECK(b.in_transaction());
  CHECK_FALSE(b.can_undo());
  b.commit();
  CHECK_FALSE(b.in_transaction());
  CHECK(b.text() == "**word**");

  const std::vector<Edit> u = b.undo();
  REQUIRE(u.size() == 2);
  CHECK(u[0].offset == 6);
  CHECK(u[1].offset == 0);
  CHECK(b.text() == "word");
  CHECK_FALSE(b.can_undo());

  const std::vector<Edit> r = b.redo();
  REQUIRE(r.size() == 2);
  CHECK(r[0].offset == 0);
  CHECK(r[1].offset == 6);
  CHECK(b.text() == "**word**");

  SECTION("an empty transaction adds no step") {
    b.begin();
    b.commit();
    b.undo();
    CHECK(b.text() == "word");
    CHECK_FALSE(b.can_undo());
  }
  SECTION("commit without begin throws") { CHECK_THROWS_AS(b.commit(), std::logic_error); }
  SECTION("undo and redo inside a transaction throw") {
    b.begin();
    CHECK_THROWS_AS(b.undo(), std::logic_error);
    CHECK_THROWS_AS(b.redo(), std::logic_error);
    b.commit();
  }
}

TEST_CASE("buffer new edit clears redo") {
  Buffer b("x");
  b.apply({1, 0, "y"});
  b.undo();
  CHECK(b.can_redo());
  b.apply({0, 0, "z"});
  CHECK_FALSE(b.can_redo());
  CHECK(b.redo().empty());
  CHECK(b.text() == "zx");
}

TEST_CASE("buffer rejects invalid ranges") {
  Buffer b("abc");
  CHECK_THROWS_AS(b.apply({4, 0, "x"}), std::out_of_range);
  CHECK_THROWS_AS(b.apply({2, 2, ""}), std::out_of_range);
  CHECK_THROWS_AS(b.apply({1, static_cast<std::size_t>(-1), ""}), std::out_of_range);
  CHECK_THROWS_AS(b.apply({static_cast<std::size_t>(-1), 2, ""}), std::out_of_range);
  CHECK_THROWS_AS(b.slice(2, 2), std::out_of_range);
  CHECK_THROWS_AS(b.slice(1, static_cast<std::size_t>(-1)), std::out_of_range);
  CHECK_THROWS_AS(b.at(3), std::out_of_range);
  CHECK_THROWS_AS(b.chunk_at(4), std::out_of_range);
  CHECK(b.text() == "abc");
  CHECK(b.version() == 0);
}

TEST_CASE("buffer rejects split code points and invalid UTF-8") {
  Buffer b("aé😀b");  
  CHECK_THROWS_AS(b.apply({2, 0, "x"}), std::invalid_argument);   
  CHECK_THROWS_AS(b.apply({1, 1, ""}), std::invalid_argument);    
  CHECK_THROWS_AS(b.apply({4, 2, ""}), std::invalid_argument);    
  CHECK_NOTHROW(b.apply({3, 4, "!"}));                            
  CHECK(b.text() == "aé!b");

  CHECK_THROWS_AS(b.apply({0, 0, "\x80"}), std::invalid_argument);
  CHECK_THROWS_AS(b.apply({0, 0, "\xC3"}), std::invalid_argument);         
  CHECK_THROWS_AS(b.apply({0, 0, "\xC0\xAF"}), std::invalid_argument);     
  CHECK_THROWS_AS(b.apply({0, 0, "\xED\xA0\x80"}), std::invalid_argument); 
  CHECK_THROWS_AS(b.apply({0, 0, "\xF4\x90\x80\x80"}), std::invalid_argument);  
  CHECK(b.text() == "aé!b");
}

TEST_CASE("buffer line helpers") {
  Buffer b("one\r\ntwo\n\nlast");
  CHECK(b.line_count() == 4);
  CHECK(b.line_start(0) == 0);
  CHECK(b.line_start(1) == 5);
  CHECK(b.line_start(2) == 9);
  CHECK(b.line_start(3) == 10);
  CHECK_THROWS_AS(b.line_start(4), std::out_of_range);
  CHECK(b.line_of(0) == 0);
  CHECK(b.line_of(4) == 0);  
  CHECK(b.line_of(5) == 1);
  CHECK(b.line_of(9) == 2);
  CHECK(b.line_of(b.size()) == 3);
  CHECK_THROWS_AS(b.line_of(b.size() + 1), std::out_of_range);

  b.apply({3, 0, "\nmid"});  
  CHECK(b.line_count() == 5);
  CHECK(b.line_start(1) == 4);
  CHECK(b.line_of(b.size()) == 4);
  CHECK(Buffer().line_count() == 1);
}

TEST_CASE("buffer random edits match a string model") {
  const std::string original = "# Titul\r\n\nPříliš 😀 漢字\ttab \nend";
  const std::vector<std::string> pool = {"", "a", "xyz", "é", "ř", "😀", "漢字", "\r\n", "\n", "\t", "  ",
                                         "**bold**"};
  std::mt19937 rng(12345);
  Buffer b(original);
  std::string model = original;

  auto boundary = [&](std::size_t max) {
    std::size_t pos = std::uniform_int_distribution<std::size_t>(0, max)(rng);
    while (pos < model.size() && pos > 0 && is_continuation(model[pos])) --pos;
    return pos;
  };

  constexpr int kSteps = 2000;
  for (int i = 0; i < kSteps; ++i) {
    const std::size_t a = boundary(model.size());
    std::size_t end = a + std::uniform_int_distribution<std::size_t>(0, 6)(rng);
    if (end > model.size()) end = model.size();
    while (end < model.size() && is_continuation(model[end])) ++end;
    std::string ins = pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(rng)];
    if (end == a && ins.empty()) ins = "+";  
    const Edit e{a, end - a, ins};
    b.apply(e);
    apply_model(model, e);
    REQUIRE(b.text() == model);
    REQUIRE(b.size() == model.size());
  }
  CHECK(read_by_chunks(b) == model);
  const std::string final_text = model;

  int undone = 0;
  while (b.can_undo()) {
    REQUIRE(b.undo().size() == 1);
    ++undone;
  }
  CHECK(undone == kSteps);
  CHECK(b.text() == original);

  int redone = 0;
  while (b.can_redo()) {
    b.redo();
    ++redone;
  }
  CHECK(redone == kSteps);
  CHECK(b.text() == final_text);
  CHECK(b.version() == static_cast<std::uint64_t>(3 * kSteps));
}

namespace {

std::vector<std::size_t> naive_line_starts(const std::string& s) {
  std::vector<std::size_t> starts{0};
  for (std::size_t i = 0; i < s.size(); ++i)
    if (s[i] == '\n') starts.push_back(i + 1);
  return starts;
}

void check_lines(const Buffer& b, const std::string& model) {
  const std::vector<std::size_t> starts = naive_line_starts(model);
  REQUIRE(b.line_count() == starts.size());
  for (std::size_t i = 0; i < starts.size(); ++i) REQUIRE(b.line_start(i) == starts[i]);
  for (std::size_t off = 0; off <= model.size(); ++off) {
    const auto line = static_cast<std::size_t>(std::upper_bound(starts.begin(), starts.end(), off) - starts.begin()) - 1;
    REQUIRE(b.line_of(off) == line);
  }
  CHECK_THROWS_AS(b.line_start(starts.size()), std::out_of_range);
}

}  

TEST_CASE("buffer line index matches a scan after random edits") {
  std::string original;
  for (int i = 0; i < 300; ++i) original += std::string(static_cast<std::size_t>(i % 37), 'x') + (i % 5 == 0 ? "\r\n" : "\n");
  const std::vector<std::string> pool = {"", "a", "\n", "\n\n", "line\nline\n", "\r\n", std::string(1500, 'y'),
                                         "z\n" + std::string(900, 'w') + "\n"};
  std::mt19937 rng(99);
  Buffer b(original);
  std::string model = original;
  check_lines(b, model);
  for (int i = 0; i < 400; ++i) {
    const std::size_t a = std::uniform_int_distribution<std::size_t>(0, model.size())(rng);
    const std::size_t end =
        std::min(model.size(), a + std::uniform_int_distribution<std::size_t>(0, i % 7 == 0 ? 3000 : 20)(rng));
    const Edit e{a, end - a, pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(rng)]};
    b.apply(e);
    apply_model(model, e);
    if (i % 10 == 0 || i > 390) {
      REQUIRE(b.text() == model);
      check_lines(b, model);
    }
    if (i % 25 == 24) {
      const std::string before = model;
      b.undo();
      b.redo();
      REQUIRE(b.text() == before);
      check_lines(b, model);
    }
  }
  CHECK(read_by_chunks(b) == model);
}

TEST_CASE("buffer piece lookup stays fast with many pieces") {
  Buffer b(std::string(100000, 'a'));
  std::string model(100000, 'a');
  std::mt19937 rng(5);
  for (int i = 0; i < 20000; ++i) {
    const std::size_t a = std::uniform_int_distribution<std::size_t>(0, model.size())(rng);
    const Edit e{a, 0, i % 3 == 0 ? "\n" : "b"};
    b.apply(e);
    apply_model(model, e);
  }
  CHECK(b.piece_count() > 10000);
  CHECK(b.text() == model);
  for (int i = 0; i < 1000; ++i) {
    const std::size_t off = std::uniform_int_distribution<std::size_t>(0, model.size() - 1)(rng);
    REQUIRE(b.at(off) == model[off]);
    REQUIRE(b.chunk_at(off).front() == model[off]);
  }
  check_lines(b, model.substr(0, model.size()));
}

TEST_CASE("buffer undo history has a limit") {
  Buffer b("x");
  CHECK(b.max_undo() == Buffer::default_max_undo);
  b.set_max_undo(3);
  for (int i = 0; i < 5; ++i) b.apply({b.size(), 0, std::to_string(i)});
  CHECK(b.text() == "x01234");
  CHECK(b.undo_steps() == 3);
  while (b.can_undo()) b.undo();
  CHECK(b.text() == "x01");  
  while (b.can_redo()) b.redo();
  CHECK(b.text() == "x01234");

  b.begin();
  b.apply({0, 0, "a"});
  b.apply({0, 0, "b"});
  b.commit();
  CHECK(b.undo_steps() == 3);

  b.set_max_undo(1);
  CHECK(b.undo_steps() == 1);
  b.set_max_undo(0);
  b.apply({0, 0, "c"});
  CHECK_FALSE(b.can_undo());
}

TEST_CASE("buffer compacts dead text") {
  const std::string big(3 << 20, 'q');  
  Buffer b(big);
  CHECK(b.store_bytes() == big.size());
  b.apply({0, big.size() - 10, ""});
  CHECK(b.text() == std::string(10, 'q'));
  CHECK(b.store_bytes() == 10);
  CHECK(b.piece_count() == 1);
  b.undo();
  CHECK(b.text() == big);

  Buffer t(std::string(1000, 'a'));
  t.set_max_undo(50);
  for (int i = 0; i < 3000; ++i) {
    t.apply({500, 0, std::string(1000, 'b')});
    t.undo();
  }
  CHECK(t.text() == std::string(1000, 'a'));
  CHECK(t.store_bytes() <= 2 * t.size() + (std::size_t{1} << 20) + 1000);
  CHECK(t.undo_steps() <= 50);
}
