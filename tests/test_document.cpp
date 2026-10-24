#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "lumos/document.hpp"
#include "lumos/parser.hpp"
#include "wipe.hpp"

using lumos::Change;
using lumos::ChangeKind;
using lumos::Document;
using lumos::Edit;

namespace {

std::string read_sample() {
  const std::filesystem::path path = std::filesystem::path(LUMOS_TEST_DATA_DIR) / "sample.md";
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.good());
  std::string out(static_cast<std::size_t>(std::filesystem::file_size(path)), '\0');
  in.read(out.data(), static_cast<std::streamsize>(out.size()));
  return out;
}

void check_tree(const Document& doc) {
  REQUIRE(doc.text() == doc.buffer().text());
  const auto problem = doc.tree().validate(doc.size());
  INFO("text: " << doc.text());
  CHECK(problem.value_or("") == "");
  auto fresh = lumos::make_parser(doc.dialect());
  fresh->parse(doc.text());
  CHECK(doc.tree().to_debug_string() == fresh->tree().to_debug_string());
}

bool is_continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0U) == 0x80U; }

std::size_t boundary(std::mt19937& rng, const std::string& text) {
  std::size_t pos = std::uniform_int_distribution<std::size_t>(0, text.size())(rng);
  while (pos < text.size() && is_continuation(text[pos])) ++pos;
  return pos;
}

Edit random_edit(std::mt19937& rng, const std::string& text) {
  static const std::vector<std::string> pieces = {
      "a", " ", "\n", "\n\n", "*", "**", "_", "`", "```\n", "# ", "- ", "1. ", "> ", "[x](y)",
      "|", "| a | b |\n|---|---|\n", "~~", "\\", "<b>", "&amp;", "ä", "日本", "🎉", "\r\n", "    "};
  const std::size_t start = boundary(rng, text);
  std::size_t end = start;
  if (std::uniform_int_distribution<int>(0, 2)(rng) == 0) {
    end = std::min(text.size(), start + std::uniform_int_distribution<std::size_t>(0, 12)(rng));
    while (end < text.size() && is_continuation(text[end])) ++end;
  }
  std::string inserted;
  const int count = std::uniform_int_distribution<int>(0, 3)(rng);
  for (int i = 0; i < count; ++i)
    inserted += pieces[std::uniform_int_distribution<std::size_t>(0, pieces.size() - 1)(rng)];
  return Edit{start, end - start, inserted};
}

}  

TEST_CASE("document load keeps the bytes and parses them") {
  const std::string sample = read_sample();
  Document doc(sample);
  CHECK(doc.text() == sample);
  CHECK_FALSE(doc.modified());
  CHECK_FALSE(doc.can_undo());
  check_tree(doc);

  std::vector<ChangeKind> kinds;
  doc.add_listener([&](const Change& c) {
    kinds.push_back(c.kind);
    CHECK(c.edits.empty());
  });
  const auto version = doc.version();
  doc.load("# New\r\n");
  CHECK(doc.text() == "# New\r\n");
  CHECK(doc.version() > version);
  CHECK(kinds == std::vector<ChangeKind>{ChangeKind::load});
  check_tree(doc);
}

TEST_CASE("document apply updates text, tree and listeners") {
  Document doc("Hello world\n");
  std::vector<std::string> seen;
  std::string shadow = doc.text();
  const auto id = doc.add_listener([&](const Change& c) {
    for (const Edit& e : c.edits) shadow.replace(e.offset, e.removed, e.inserted);
    seen.push_back(shadow);
  });

  doc.apply(Edit{6, 0, "**"});
  doc.apply(Edit{13, 0, "**"});
  CHECK(doc.text() == "Hello **world**\n");
  CHECK(shadow == doc.text());
  check_tree(doc);
  CHECK(doc.tree().to_debug_string().find("strong") != std::string::npos);

  const auto version = doc.version();
  doc.apply(Edit{3, 0, ""});
  CHECK(doc.version() == version);
  CHECK(seen.size() == 2);

  CHECK_THROWS_AS(doc.apply(Edit{100, 0, "x"}), std::out_of_range);
  CHECK(doc.text() == "Hello **world**\n");

  doc.remove_listener(id);
  doc.apply(Edit{0, 0, "# "});
  CHECK(seen.size() == 2);
  check_tree(doc);
}

TEST_CASE("document undo and redo feed every edit to the parser") {
  Document doc("a\n");
  std::string shadow = doc.text();
  std::vector<ChangeKind> kinds;
  doc.add_listener([&](const Change& c) {
    kinds.push_back(c.kind);
    for (const Edit& e : c.edits) shadow.replace(e.offset, e.removed, e.inserted);
  });

  doc.begin();
  doc.apply(Edit{0, 0, "# "});
  doc.apply(Edit{3, 0, " *b*"});
  doc.apply(Edit{doc.size(), 0, "\n```\ncode\n```\n"});
  doc.commit();
  CHECK(doc.text() == "# a *b*\n\n```\ncode\n```\n");
  check_tree(doc);

  const auto undone = doc.undo();
  CHECK(undone.size() == 3);
  CHECK(doc.text() == "a\n");
  CHECK(shadow == doc.text());
  check_tree(doc);
  CHECK(doc.can_redo());

  const auto redone = doc.redo();
  CHECK(redone.size() == 3);
  CHECK(doc.text() == "# a *b*\n\n```\ncode\n```\n");
  CHECK(shadow == doc.text());
  check_tree(doc);
  CHECK(kinds.back() == ChangeKind::redo);

  CHECK(doc.redo().empty());

  doc.begin();
  CHECK_THROWS_AS(doc.undo(), std::logic_error);
  CHECK_THROWS_AS(doc.mark_saved(), std::logic_error);
  CHECK_THROWS_AS(doc.load("x"), std::logic_error);
  doc.commit();
}

TEST_CASE("document modified flag follows undo and redo") {
  Document doc("text");
  CHECK_FALSE(doc.modified());
  doc.apply(Edit{4, 0, "1"});
  CHECK(doc.modified());
  doc.undo();
  CHECK_FALSE(doc.modified());
  doc.redo();
  CHECK(doc.modified());
  doc.mark_saved();
  CHECK_FALSE(doc.modified());
  doc.undo();
  CHECK(doc.modified());
  doc.redo();
  CHECK_FALSE(doc.modified());

  doc.begin();
  CHECK_FALSE(doc.modified());
  doc.apply(Edit{0, 0, "x"});
  CHECK(doc.modified());
  doc.commit();
  CHECK(doc.modified());

  doc.undo();
  doc.undo();
  doc.apply(Edit{0, 0, "y"});
  CHECK(doc.modified());
  doc.undo();
  CHECK(doc.modified());  

  Document other("a");
  other.begin();
  other.commit();
  CHECK_FALSE(other.can_undo());
  CHECK_FALSE(other.modified());
}

TEST_CASE("document listener may remove itself") {
  Document doc("a");
  int calls = 0;
  Document::ListenerId id = 0;
  id = doc.add_listener([&](const Change&) {
    ++calls;
    doc.remove_listener(id);
  });
  doc.apply(Edit{0, 0, "b"});
  doc.apply(Edit{0, 0, "c"});
  CHECK(calls == 1);
}

TEST_CASE("document random edits, undo and redo keep the tree right") {
  for (unsigned seed = 1; seed <= 6; ++seed) {
    std::mt19937 rng(seed);
    Document doc(seed % 2 == 0 ? read_sample() : std::string("# Title\n\nSome *text*.\n"));
    std::string shadow = doc.text();
    doc.add_listener([&](const Change& c) {
      if (c.kind == ChangeKind::load) shadow = doc.text();
      for (const Edit& e : c.edits) shadow.replace(e.offset, e.removed, e.inserted);
    });
    std::vector<std::string> history{doc.text()};
    for (int step = 0; step < 60; ++step) {
      INFO("seed " << seed << " step " << step);
      const int op = std::uniform_int_distribution<int>(0, 9)(rng);
      if (op < 5) {
        const Edit e = random_edit(rng, doc.text());
        const bool empty = e.removed == 0 && e.inserted.empty();
        doc.apply(e);
        if (!empty) history.push_back(doc.text());
      } else if (op < 7) {
        doc.begin();
        bool any = false;
        for (int i = 0; i < 3; ++i) {
          const Edit e = random_edit(rng, doc.text());
          any = any || e.removed != 0 || !e.inserted.empty();
          doc.apply(e);
        }
        doc.commit();
        if (any) history.push_back(doc.text());
      } else if (op < 9) {
        if (!doc.undo().empty()) history.pop_back();
        CHECK(doc.text() == history.back());
      } else {
        const bool did = !doc.redo().empty();
        if (did) history.push_back(doc.text());
      }
      CHECK(shadow == doc.text());
      check_tree(doc);
    }
    while (doc.can_undo()) doc.undo();
    CHECK(doc.text() == history.front());
    CHECK_FALSE(doc.modified());
    check_tree(doc);
  }
}

TEST_CASE("document undo limit keeps the modified flag right") {
  Document doc("a");
  doc.set_max_undo(2);
  doc.apply(Edit{1, 0, "b"});
  doc.mark_saved();  
  doc.apply(Edit{2, 0, "c"});
  doc.apply(Edit{3, 0, "d"});  
  CHECK(doc.text() == "abcd");
  while (doc.can_undo()) doc.undo();
  CHECK(doc.text() == "ab");
  CHECK_FALSE(doc.modified());  
  doc.redo();
  CHECK(doc.modified());

  Document other("x");
  other.set_max_undo(1);
  other.apply(Edit{1, 0, "1"});
  other.apply(Edit{2, 0, "2"});
  other.undo();
  CHECK(other.text() == "x1");
  CHECK_FALSE(other.can_undo());
  CHECK(other.modified());
  check_tree(other);
}

TEST_CASE("document change reports the tree regions") {
  Document doc("# Title\n\nSome text.\n\nMore text.\n");
  std::vector<lumos::TextRegion> regions;
  doc.add_listener([&](const Change& c) { regions.assign(c.tree_changes.begin(), c.tree_changes.end()); });

  doc.apply(Edit{14, 0, "x"});  
  REQUIRE(regions.size() == 1);
  CHECK(regions[0].start <= 14);
  CHECK(regions[0].new_end >= 15);
  CHECK(regions[0].new_end - regions[0].old_end == 1);
  check_tree(doc);

  doc.load("new\n");
  REQUIRE(regions.size() == 1);
  CHECK(regions[0].start == 0);
  CHECK(regions[0].new_end == 4);
}

TEST_CASE("document wipe empties the text, the history and the labels") {
  Document doc("[a]: /x\n\ntext [a]\n");
  doc.apply(Edit{0, 0, "secret "});
  doc.apply(Edit{0, 7, ""});
  doc.undo();  
  const auto version = doc.version();
  std::vector<ChangeKind> kinds;
  doc.add_listener([&](const Change& c) { kinds.push_back(c.kind); });

  doc.wipe();
  CHECK(doc.size() == 0);
  CHECK(doc.buffer().store_bytes() == 0);
  CHECK_FALSE(doc.can_undo());
  CHECK_FALSE(doc.can_redo());
  CHECK_FALSE(doc.modified());
  CHECK(doc.version() > version);
  CHECK(doc.tree().size() == 1);
  REQUIRE(kinds.size() == 1);
  CHECK(kinds[0] == ChangeKind::load);

  doc.apply(Edit{0, 0, "[a]\n"});
  check_tree(doc);
  for (lumos::NodeId i = 0; i < doc.tree().size(); ++i) CHECK(doc.tree().node(i).kind != lumos::NodeKind::link);

  doc.begin();
  CHECK_THROWS_AS(doc.wipe(), std::logic_error);
  doc.commit();
}

TEST_CASE("wipe_bytes and wipe_string overwrite and free") {
  std::string buf(64, 'y');
  lumos::wipe_bytes(buf.data(), buf.size());
  CHECK(buf == std::string(64, '\0'));
  std::string s(100, 'x');
  lumos::wipe_string(s);
  CHECK(s.empty());
}
