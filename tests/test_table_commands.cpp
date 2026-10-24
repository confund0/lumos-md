#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <exception>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "lumos/commands.hpp"
#include "lumos/document.hpp"
#include "lumos/log.hpp"
#include "lumos/syntax.hpp"

using namespace lumos;

namespace {

std::optional<CommandResult> next_cell_lf(const Document& d, Selection s) { return next_cell(d, s, "\n"); }
std::optional<CommandResult> next_row_lf(const Document& d, Selection s) { return next_row(d, s, "\n"); }

template <typename Cmd>
std::string run(Cmd cmd, const std::string& marked) {
  std::string text;
  std::size_t cursor = std::string::npos;
  std::size_t anchor = std::string::npos;
  for (char c : marked) {
    if (c == '@') {
      cursor = text.size();
    } else if (c == '^') {
      anchor = text.size();
    } else {
      text += c;
    }
  }
  REQUIRE(cursor != std::string::npos);
  if (anchor == std::string::npos) anchor = cursor;
  Document doc(text);
  const auto r = cmd(doc, Selection{anchor, cursor});
  if (!r) return "none";
  doc.begin();
  for (const Edit& e : r->edits) doc.apply(e);
  doc.commit();
  std::string out = doc.text();
  const Selection s = r->selection;
  REQUIRE(s.cursor <= out.size());
  REQUIRE(s.anchor <= out.size());
  if (s.anchor != s.cursor) {
    if (s.anchor > s.cursor) {
      out.insert(s.anchor, "^");
      out.insert(s.cursor, "@");
    } else {
      out.insert(s.cursor, "@");
      out.insert(s.anchor, "^");
    }
  } else {
    out.insert(s.cursor, "@");
  }
  return out;
}

}  

namespace {

std::optional<CommandResult> ins_1x1(const Document& d, Selection s) { return insert_table(d, s, 1, 1, "\n"); }
std::optional<CommandResult> ins_1x3(const Document& d, Selection s) { return insert_table(d, s, 1, 3, "\n"); }
std::optional<CommandResult> ins_3x1(const Document& d, Selection s) { return insert_table(d, s, 3, 1, "\n"); }
std::optional<CommandResult> ins_2x2(const Document& d, Selection s) { return insert_table(d, s, 2, 2, "\n"); }

}  

TEST_CASE("commands: insert_table on an empty document (rows=1, a header only)") {
  CHECK(run(ins_1x1, "@") == "| @    |\n| --- |");
}

TEST_CASE("commands: insert_table on a blank line between paragraphs (columns=3)") {
  CHECK(run(ins_1x3, "para1\n@\npara2\n") ==
        "para1\n| @    |     |     |\n| --- | --- | --- |\n\npara2\n");
}

TEST_CASE("commands: insert_table on a paragraph goes after it (rows=3, columns=1)") {
  CHECK(run(ins_3x1, "para1@\n") == "para1\n| @    |\n| --- |\n|     |\n|     |\n");
  CHECK(run(ins_1x1, "para1@") == "para1\n| @    |\n| --- |\n");
}

TEST_CASE("commands: insert_table on a blank quote line keeps the '>' on every row") {
  CHECK(run(ins_2x2, "> text\n> @\n") ==
        "> text\n> | @    |     |\n> | --- | --- |\n> |     |     |\n");
}

TEST_CASE("commands: insert_table in a list item keeps its indentation") {
  CHECK(run(ins_2x2, "- item@\n") ==
        "- item\n  | @    |     |\n  | --- | --- |\n  |     |     |\n");
}

TEST_CASE("commands: insert_table on an empty line under a list item goes outside the list") {
  CHECK(run(ins_2x2, "- list\n- items\n@") ==
        "- list\n- items\n\n| @    |     |\n| --- | --- |\n|     |     |");
  CHECK(run(ins_2x2, "- items\n@\ntext\n") ==
        "- items\n\n| @    |     |\n| --- | --- |\n|     |     |\n\ntext\n");
}

TEST_CASE("commands: insert_table on an empty item or its indented line goes in the item") {
  CHECK(run(ins_2x2, "- a\n- @") == "- a\n- | @    |     |\n  | --- | --- |\n  |     |     |");
  CHECK(run(ins_2x2, "- a\n- @\n- c\n") ==
        "- a\n- | @    |     |\n  | --- | --- |\n  |     |     |\n- c\n");
  CHECK(run(ins_2x2, "- a\n-\n  @") == "- a\n-\n  | @    |     |\n  | --- | --- |\n  |     |     |");
  CHECK(run(ins_2x2, "- a\n\n  @") == "- a\n\n  | @    |     |\n  | --- | --- |\n  |     |     |");
  CHECK(run(ins_2x2, "- a\n\n @") == "- a\n\n| @    |     |\n| --- | --- |\n|     |     |");
}

TEST_CASE("commands: insert_table does not apply in a code block or a table") {
  CHECK(run(ins_1x1, "    code@\n") == "none");
  CHECK(run(ins_1x1, "| a | b |\n| --- | --- |\n| @c | d |\n") == "none");
}

TEST_CASE("commands: insert_table does not apply with rows or columns under 1") {
  const Document doc("text\n");
  const Selection sel{0, 0};
  CHECK_FALSE(insert_table(doc, sel, 0, 1, "\n"));
  CHECK_FALSE(insert_table(doc, sel, 1, 0, "\n"));
}

TEST_CASE("commands: Tab moves to the next cell, skipping the delimiter row") {
  CHECK(run(next_cell_lf, "| @aaa | bbb |\n| --- | --- |\n| ccc | ddd |\n") ==
        "| aaa | ^bbb@ |\n| --- | --- |\n| ccc | ddd |\n");
  CHECK(run(next_cell_lf, "| aaa | @bbb |\n| --- | --- |\n| ccc | ddd |\n") ==
        "| aaa | bbb |\n| --- | --- |\n| ^ccc@ | ddd |\n");
  CHECK(run(next_cell_lf, "| aaa | bbb |\n| --- | --- |\n| ccc | @ddd |\n") ==
        "| aaa | bbb |\n| --- | --- |\n| ccc | ddd |\n"
        "| @    |     |\n");
}

TEST_CASE("commands: Shift+Tab moves to the previous cell") {
  CHECK(run(previous_cell, "| aaa | @bbb |\n| --- | --- |\n| ccc | ddd |\n") ==
        "| ^aaa@ | bbb |\n| --- | --- |\n| ccc | ddd |\n");
  CHECK(run(previous_cell, "| aaa | bbb |\n| --- | --- |\n| @ccc | ddd |\n") ==
        "| aaa | ^bbb@ |\n| --- | --- |\n| ccc | ddd |\n");
  CHECK(run(previous_cell, "| @aaa | bbb |\n| --- | --- |\n| ccc | ddd |\n") ==
        "| @aaa | bbb |\n| --- | --- |\n| ccc | ddd |\n");
}

TEST_CASE("commands: Enter moves to the next row, same column") {
  CHECK(run(next_row_lf, "| @aaa | bbb |\n| --- | --- |\n| ccc | ddd |\n") ==
        "| aaa | bbb |\n| --- | --- |\n| ^ccc@ | ddd |\n");

  CHECK(run(next_row_lf, "| aaa | bbb |\n| --- | --- |\n| ccc | @ddd |") ==
        "| aaa | bbb |\n| --- | --- |\n| ccc | ddd |\n\n@");

  CHECK(run(next_row_lf, "| aaa | bbb |\n| --- | --- |\n| ccc | @ddd |\n\ntext\n") ==
        "| aaa | bbb |\n| --- | --- |\n| ccc | ddd |\n\n@\n\ntext\n");

  CHECK(run(next_row_lf, "| aaa | bbb |\n| --- | --- |\n| ccc | @ddd |\n\n\n") ==
        "| aaa | bbb |\n| --- | --- |\n| ccc | ddd |\n\n@\n");
}

TEST_CASE("commands: Shift+Enter moves to the previous row, same column") {
  CHECK(run(previous_row, "| aaa | bbb |\n| --- | --- |\n| @ccc | ddd |\n") ==
        "| ^aaa@ | bbb |\n| --- | --- |\n| ccc | ddd |\n");
  CHECK(run(previous_row, "| @aaa | bbb |\n| --- | --- |\n| ccc | ddd |\n") ==
        "| @aaa | bbb |\n| --- | --- |\n| ccc | ddd |\n");
}

TEST_CASE("commands: the column at the cursor") {
  CHECK(run(next_cell_lf, "| aaa @  | bbb |\n| --- | --- |\n| ccc | ddd |\n") ==
        "| aaa | ^bbb@ |\n| --- | --- |\n| ccc | ddd |\n");
  CHECK(run(next_cell_lf, "| aaa@| bbb |\n| --- | --- |\n| ccc | ddd |\n") ==
        "| aaa | ^bbb@ |\n| --- | --- |\n| ccc | ddd |\n");
  CHECK(run(next_cell_lf, "a@ | b\n--- | ---\nc | d\n") ==
        "| a   | ^b@   |\n| --- | --- |\n| c   | d   |\n");
  CHECK(run(next_cell_lf, "| a | b |\n| --- | --- |\n| c | d | e@ |\n") ==
        "| a   | b   |\n"
        "| --- | --- |\n"
        "| c   | d   | e   |\n"
        "| @    |     |\n");
}

TEST_CASE("commands: format_table aligns right, center, left and none, codepoints not bytes") {
  Document doc(
      "| a | cc | \xc3\xa4 | f |\n"
      "| :-- | :-: | --: | --- |\n"
      "| bbbb | d | eee |\n"
      "| x | y | z | w | extra |\n");
  const auto r = format_table(doc, {0, 0});
  REQUIRE(r);
  CHECK_FALSE(r->edits.empty());
  doc.begin();
  for (const Edit& e : r->edits) doc.apply(e);
  doc.commit();
  CHECK(doc.text() ==
        "| " "a   " " | " "cc " " | " "  \xc3\xa4" " | " "f  " " |\n"
        "| " ":---" " | " ":-:" " | " "--:" " | " "---" " |\n"
        "| " "bbbb" " | " " d " " | " "eee" " | " "   " " |\n"
        "| " "x   " " | " " y " " | " "  z" " | " "w  " " | " "extra" " |\n");
}

TEST_CASE("commands: a table in a block quote and in a list item") {
  CHECK(run(next_cell_lf, "> | aaa | bbb |\n> | --- | --- |\n> | ccc | @ddd |\n") ==
        "> | aaa | bbb |\n> | --- | --- |\n> | ccc | ddd |\n"
        "> | @    |     |\n");
  CHECK(run(next_cell_lf, "- | aaa | bbb |\n  | --- | --- |\n  | ccc | @ddd |\n") ==
        "- | aaa | bbb |\n  | --- | --- |\n  | ccc | ddd |\n"
        "  | @    |     |\n");
  CHECK(run(next_row_lf, "> | aaa | bbb |\n> | --- | --- |\n> | ccc | @ddd |") ==
        "> | aaa | bbb |\n> | --- | --- |\n> | ccc | ddd |\n>\n> @");
}

TEST_CASE("commands: CRLF table rows") {
  CHECK(run(next_cell_lf, "| aaa | bbb |\r\n| --- | --- |\r\n| ccc | @ddd |\r\n") ==
        "| aaa | bbb |\r\n| --- | --- |\r\n| ccc | ddd |\r\n"
        "| @    |     |\r\n");
}

TEST_CASE("commands: an aligned table gives no edits") {
  const Document doc("| aaa | bbb |\n| --- | --- |\n| ccc | ddd |\n");
  const auto r = format_table(doc, {5, 5});
  REQUIRE(r);
  CHECK(r->edits.empty());
}

TEST_CASE("commands: not in a table gives nullopt") {
  const Document doc("plain text\n");
  const Selection sel{2, 2};
  CHECK_FALSE(next_cell(doc, sel, "\n"));
  CHECK_FALSE(previous_cell(doc, sel));
  CHECK_FALSE(next_row(doc, sel, "\n"));
  CHECK_FALSE(previous_row(doc, sel));
  CHECK_FALSE(format_table(doc, sel));
}

namespace {

bool boundary(const std::string& s, std::size_t i) {
  return i == s.size() || (static_cast<unsigned char>(s[i]) & 0xC0U) != 0x80U;
}

std::string random_table_document(std::mt19937& rng) {
  static const std::vector<std::string> cell_texts = {"a",  "bb",  "ccc", "\xc3\xa4", "d\\|e", "`code`", ""};
  auto cell = [&]() { return cell_texts[std::uniform_int_distribution<std::size_t>(0, cell_texts.size() - 1)(rng)]; };
  const int cols = std::uniform_int_distribution<int>(1, 3)(rng);
  const bool outer = cols == 1 || std::uniform_int_distribution<int>(0, 1)(rng) == 0;
  const bool crlf = std::uniform_int_distribution<int>(0, 4)(rng) == 0;
  const std::string nl = crlf ? "\r\n" : "\n";
  const int container = std::uniform_int_distribution<int>(0, 2)(rng);  
  auto prefix = [&](bool first) {
    if (container == 1) return std::string("> ");
    if (container == 2) return first ? std::string("- ") : std::string("  ");
    return std::string();
  };
  static const Alignment align_pool[] = {Alignment::none, Alignment::left, Alignment::center, Alignment::right};
  std::vector<Alignment> aligns;
  for (int i = 0; i < cols; ++i) aligns.push_back(align_pool[std::uniform_int_distribution<int>(0, 3)(rng)]);

  auto write_row = [&](const std::vector<std::string>& cells, bool first) {
    std::string r = prefix(first);
    if (outer) r += "| ";
    for (std::size_t i = 0; i < cells.size(); ++i) {
      if (i > 0) r += " | ";
      r += cells[i];
    }
    if (outer) r += " |";
    return r;
  };
  std::vector<std::string> header;
  for (int i = 0; i < cols; ++i) header.push_back(cell());

  std::string out;
  out += write_row(header, true) + nl;
  std::string delim = prefix(false);
  if (outer) delim += "| ";
  for (int i = 0; i < cols; ++i) {
    if (i > 0) delim += " | ";
    switch (aligns[static_cast<std::size_t>(i)]) {
      case Alignment::left: delim += ":--"; break;
      case Alignment::right: delim += "--:"; break;
      case Alignment::center: delim += ":-:"; break;
      default: delim += "---"; break;
    }
  }
  if (outer) delim += " |";
  out += delim + nl;

  const int body_count = std::uniform_int_distribution<int>(0, 3)(rng);
  for (int b = 0; b < body_count; ++b) {
    int n = cols;
    const int variant = std::uniform_int_distribution<int>(0, 2)(rng);
    if (variant == 1 && cols > 1) n = cols - 1;
    if (variant == 2) n = cols + 1;
    std::vector<std::string> row;
    for (int i = 0; i < n; ++i) row.push_back(cell());
    out += write_row(row, false) + nl;
  }

  if (container == 0) {
    if (std::uniform_int_distribution<int>(0, 1)(rng) == 0) out = "before\n\n" + out;
    if (std::uniform_int_distribution<int>(0, 1)(rng) == 0) out += "\nafter\n";
  }
  return out;
}

std::optional<std::pair<std::size_t, std::size_t>> table_range(const Document& doc, std::size_t pos) {
  const SyntaxTree& tree = doc.tree();
  for (NodeId cur = tree.node_at(pos > 0 ? pos - 1 : 0); cur != no_node; cur = tree[cur].parent) {
    if (tree[cur].kind == NodeKind::table) return std::pair{tree[cur].start, tree[cur].end};
  }
  return std::nullopt;
}

template <typename Cmd>
void check_table_cmd(const std::string& text, Selection sel, const char* name, Cmd cmd, int* applied) {
  Document doc(text);
  const auto r = cmd(doc, sel);
  if (!r) return;
  std::string esc;
  for (char c : text) esc += c == '\n' ? std::string("\\n") : c == '\r' ? std::string("\\r") : std::string(1, c);
  INFO(name << " text: [" << esc << "] anchor: " << sel.anchor << " cursor: " << sel.cursor);
  const auto range = table_range(doc, sel.cursor);
  REQUIRE(range);
  doc.begin();
  for (const Edit& e : r->edits) REQUIRE_NOTHROW(doc.apply(e));
  doc.commit();
  REQUIRE(r->selection.cursor <= doc.size());
  REQUIRE(r->selection.anchor <= doc.size());
  REQUIRE_FALSE(doc.tree().validate(doc.size()).has_value());
  if (applied) ++*applied;
  const std::string new_text = doc.text();
  CHECK(new_text.substr(0, range->first) == text.substr(0, range->first));
  const std::string after_old = text.substr(range->second);
  REQUIRE(new_text.size() >= after_old.size());
  CHECK(new_text.substr(new_text.size() - after_old.size()) == after_old);
  std::optional<CommandResult> again;
  try {
    again = format_table(doc, r->selection);
  } catch (const std::exception& ex) {
    FAIL("format_table threw: " << ex.what() << " on doc: " << doc.text() << " sel: " << r->selection.cursor);
  }
  if (again) CHECK(again->edits.empty());
  if (!r->edits.empty()) {
    doc.undo();
    CHECK(doc.text() == text);
  }
}

void check_insert_table(const std::string& text, Selection sel, int* applied) {
  Document doc(text);
  const auto r = insert_table(doc, sel, 2, 2, "\n");
  if (!r) return;
  std::string esc;
  for (char c : text) esc += c == '\n' ? std::string("\\n") : c == '\r' ? std::string("\\r") : std::string(1, c);
  INFO("insert_table text: [" << esc << "] anchor: " << sel.anchor << " cursor: " << sel.cursor);
  doc.begin();
  for (const Edit& e : r->edits) REQUIRE_NOTHROW(doc.apply(e));
  doc.commit();
  REQUIRE(r->selection.cursor <= doc.size());
  REQUIRE(r->selection.anchor <= doc.size());
  REQUIRE_FALSE(doc.tree().validate(doc.size()).has_value());
  CHECK(table_range(doc, r->selection.cursor).has_value());
  ++*applied;
  doc.undo();
  CHECK(doc.text() == text);
}

}  

TEST_CASE("commands: property check of the table commands over random documents", "[random]") {
  std::mt19937 rng(2024);
  int applied = 0;
  int inserted = 0;
  for (int round = 0; round < 300; ++round) {
    const std::string text = random_table_document(rng);
    for (std::size_t pos = 0; pos <= text.size(); ++pos) {
      if (!boundary(text, pos)) continue;
      const Selection sel{pos, pos};
      check_table_cmd(text, sel, "next_cell", next_cell_lf, &applied);
      check_table_cmd(text, sel, "previous_cell", previous_cell, &applied);
      check_table_cmd(text, sel, "next_row", next_row_lf, &applied);
      check_table_cmd(text, sel, "previous_row", previous_row, &applied);
      check_table_cmd(text, sel, "format_table", format_table, &applied);
      check_insert_table(text, sel, &inserted);
    }
  }
  WARN("table commands: " << applied << " applied");
  WARN("insert_table: " << inserted << " applied");
  CHECK(applied > 500);
  CHECK(inserted > 1500);
}
