#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <cstddef>
#include <optional>
#include <random>
#include <sstream>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "lumos/commands.hpp"
#include "lumos/document.hpp"
#include "lumos/log.hpp"

using namespace lumos;

namespace {

using Command = std::optional<CommandResult> (*)(const Document&, Selection);

std::optional<CommandResult> enter_lf(const Document& d, Selection s) { return insert_newline(d, s, "\n"); }
std::optional<CommandResult> enter_crlf(const Document& d, Selection s) { return insert_newline(d, s, "\r\n"); }
std::optional<CommandResult> break_lf(const Document& d, Selection s) { return insert_line_break(d, s, "\n"); }
std::optional<CommandResult> break_crlf(const Document& d, Selection s) { return insert_line_break(d, s, "\r\n"); }

template <typename Cmd>
std::string run(Cmd cmd, const std::string& marked, const Dialect& dialect = {}) {
  std::string text;
  std::size_t cursor = std::string::npos;
  std::size_t anchor = std::string::npos;
  for (char c : marked) {
    if (c == '|') {
      cursor = text.size();
    } else if (c == '^') {
      anchor = text.size();
    } else {
      text += c;
    }
  }
  REQUIRE(cursor != std::string::npos);
  if (anchor == std::string::npos) anchor = cursor;
  Document doc(text, dialect);
  const auto r = cmd(doc, {anchor, cursor});
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
      out.insert(s.cursor, "|");
    } else {
      out.insert(s.cursor, "|");
      out.insert(s.anchor, "^");
    }
  } else {
    out.insert(s.cursor, "|");
  }
  return out;
}

}  

TEST_CASE("commands: Enter continues a list item") {
  CHECK(run(enter_lf, "- a|") == "- a\n- |");
  CHECK(run(enter_lf, "* a|") == "* a\n* |");
  CHECK(run(enter_lf, "+ a|") == "+ a\n+ |");
  CHECK(run(enter_lf, "1. a|") == "1. a\n2. |");
  CHECK(run(enter_lf, "9) a|") == "9) a\n10) |");
  CHECK(run(enter_lf, "-   a|") == "-   a\n-   |");
  CHECK(run(enter_lf, "  - a|") == "  - a\n  - |");
  CHECK(run(enter_lf, "- a\n  - b|") == "- a\n  - b\n  - |");
  CHECK(run(enter_lf, "1. a\n   1. b|") == "1. a\n   1. b\n   2. |");
}

TEST_CASE("commands: Enter continues a task item unchecked") {
  CHECK(run(enter_lf, "- [x] a|") == "- [x] a\n- [ ] |");
  CHECK(run(enter_lf, "- [ ] a|") == "- [ ] a\n- [ ] |");
  CHECK(run(enter_lf, "1. [X] a|") == "1. [X] a\n2. [ ] |");
}

TEST_CASE("commands: Enter splits an item and drops trailing spaces") {
  CHECK(run(enter_lf, "- a|b") == "- a\n- |b");
  CHECK(run(enter_lf, "- a  |") == "- a\n- |");
}

TEST_CASE("commands: Enter renumbers the next items") {
  CHECK(run(enter_lf, "1. a|\n2. b") == "1. a\n2. |\n3. b");
  CHECK(run(enter_lf, "1. a\n2. b|\n3. c") == "1. a\n2. b\n3. |\n4. c");
  CHECK(run(enter_lf, "1) a|b\n2) c") == "1) a\n2) |b\n3) c");
  CHECK(run(enter_lf, "1. a|\n2. b\n5. c") == "1. a\n2. |\n3. b\n5. c");
  CHECK(run(enter_lf, "1. a|\n3. b") == "1. a\n2. |\n3. b");
  CHECK(run(enter_lf, "1. a\n   1. b|\n   2. c\n2. d") == "1. a\n   1. b\n   2. |\n   3. c\n2. d");
  CHECK(run(enter_lf, "8. a|\n9. b\n   - c") == "8. a\n9. |\n10. b\n    - c");
  CHECK(run(enter_lf, "1. a\n   - b\n   - |\n2. c") == "1. a\n   - b\n2. |\n3. c");
  CHECK(run(enter_lf, "- a|\n- b") == "- a\n- |\n- b");
}

TEST_CASE("commands: Enter in a loose list adds a blank line") {
  CHECK(run(enter_lf, "- a\n\n- b|") == "- a\n\n- b\n\n- |");
  CHECK(run(enter_lf, "> - a\n>\n> - b|") == "> - a\n>\n> - b\n>\n> - |");
}

TEST_CASE("commands: Enter on a continuation line keeps the indentation") {
  CHECK(run(enter_lf, "- a\n  b|") == "- a\n  b\n  |");
}

TEST_CASE("commands: Enter on an empty item ends the list") {
  CHECK(run(enter_lf, "- a\n- |") == "- a\n|");
  CHECK(run(enter_lf, "- a\n- |\n- b") == "- a\n|\n- b");
  CHECK(run(enter_lf, "- [ ] a\n- [ ] |") == "- [ ] a\n|");
  CHECK(run(enter_lf, "> - a\n> - |") == "> - a\n> |");
}

TEST_CASE("commands: Enter on an empty nested item goes one level out") {
  CHECK(run(enter_lf, "- a\n  - b\n  - |") == "- a\n  - b\n- |");
  CHECK(run(enter_lf, "1. a\n   - b\n   - |") == "1. a\n   - b\n2. |");
  CHECK(run(enter_lf, "1.\n   1. b\n   2. |") == "1.\n   1. b\n2. |");
  CHECK(run(enter_lf, "-\n  - b\n  - |") == "-\n  - b\n- |");
}

TEST_CASE("commands: Enter in a block quote") {
  CHECK(run(enter_lf, "> a|") == "> a\n> |");
  CHECK(run(enter_lf, "> > a|") == "> > a\n> > |");
  CHECK(run(enter_lf, "> - a|") == "> - a\n> - |");
  CHECK(run(enter_lf, "- > a|") == "- > a\n  > |");
  CHECK(run(enter_lf, "> a\n> |") == "> a\n>\n> |");
  CHECK(run(enter_lf, "> a\n>\n> |") == "> a\n\n|");
}

TEST_CASE("commands: Enter in a code block keeps the indentation") {
  CHECK(run(enter_lf, "```\n  x|\n```") == "```\n  x\n  |\n```");
  CHECK(run(enter_lf, "```\nx|\n```") == "none");
  CHECK(run(enter_lf, "    code|") == "    code\n    |");
  CHECK(run(enter_lf, "- ```\n  x|\n  ```") == "- ```\n  x\n  |\n  ```");
  CHECK(run(enter_lf, "- ```|") == "- ```\n  |");
  CHECK(run(enter_lf, "-     code|") == "-     code\n      |");
  CHECK(run(enter_lf, "> ```\n>   x|\n> ```") == "> ```\n>   x\n>   |\n> ```");
  CHECK(run(enter_lf, "```\n> x|\n```") == "none");
}

TEST_CASE("commands: Enter elsewhere does its default") {
  CHECK(run(enter_lf, "a|") == "none");
  CHECK(run(enter_lf, "# a|") == "none");
  CHECK(run(enter_lf, "|- a") == "none");
  CHECK(run(enter_lf, "- ^a|") == "none");
  CHECK(run(enter_lf, "- a\n|") == "none");
}

TEST_CASE("commands: Enter writes the given line ending") {
  CHECK(run(enter_crlf, "- a|\r\n- b") == "- a\r\n- |\r\n- b");
  CHECK(run(enter_crlf, "> a\r\n>\r\n> |") == "> a\r\n\r\n|");
}

TEST_CASE("commands: Shift+Enter in a list item indents to the content column") {
  CHECK(run(break_lf, "- a|") == "- a\\\n  |");
  CHECK(run(break_lf, "1. a|") == "1. a\\\n   |");
  CHECK(run(break_lf, "10) a|") == "10) a\\\n    |");
  CHECK(run(break_lf, "-   a|") == "-   a\\\n    |");
  CHECK(run(break_lf, "1. ab|cd") == "1. ab\\\n   |cd");
  CHECK(run(break_lf, "- a  |") == "- a\\\n  |");
  CHECK(run(break_lf, "- a\n  - b|") == "- a\n  - b\\\n    |");
  CHECK(run(break_lf, "- a\n  b|") == "- a\n  b\\\n  |");
  CHECK(run(break_lf, "- a\nb|") == "- a\nb\\\n  |");
  CHECK(run(break_lf, "- [ ] a|") == "- [ ] a\\\n  |");
  CHECK(run(break_lf, "1. a|\n2. b") == "1. a\\\n   |\n2. b");
}

TEST_CASE("commands: Shift+Enter on an empty item") {
  CHECK(run(break_lf, "1. |") == "1.\n   |");
  CHECK(run(break_lf, "- a\n- |") == "- a\n-\n  |");
}

TEST_CASE("commands: Shift+Enter in a block quote") {
  CHECK(run(break_lf, "> a|") == "> a\\\n> |");
  CHECK(run(break_lf, "> > a|") == "> > a\\\n> > |");
  CHECK(run(break_lf, "> - a|") == "> - a\\\n>   |");
  CHECK(run(break_lf, "- > a|") == "- > a\\\n  > |");
  CHECK(run(break_lf, "> a\n>\n> |") == "> a\n>\n>\n> |");
}

TEST_CASE("commands: Shift+Enter in a paragraph is a hard break") {
  CHECK(run(break_lf, "a|") == "a\\\n|");
  CHECK(run(break_lf, "ab|cd") == "ab\\\n|cd");
  CHECK(run(break_lf, "**a|**") == "**a\\\n|**");
  CHECK(run(break_lf, "a\\|") == "none");
  CHECK(run(break_lf, "- a\\|") == "- a\\\n  |");
  CHECK(run(break_lf, "a\\\\|") == "a\\\\\\\n|");
  CHECK(run(break_lf, "|a") == "none");
  CHECK(run(break_lf, "`a|b`") == "none");
  CHECK(run(break_lf, "<http://a|b>") == "none");
  CHECK(run(break_lf, "- `a|b`") == "- `a\n  |b`");
}

TEST_CASE("commands: Shift+Enter in a heading or code block") {
  CHECK(run(break_lf, "# a|") == "none");
  CHECK(run(break_lf, "- # a|") == "- # a\n  |");
  CHECK(run(break_lf, "```\n  x|\n```") == "```\n  x\n  |\n```");
  CHECK(run(break_lf, "    code|") == "    code\n    |");
  CHECK(run(break_lf, "- ```\n  x|\n  ```") == "- ```\n  x\n  |\n  ```");
}

TEST_CASE("commands: Shift+Enter in a table does nothing") {
  const Document doc("| a |\n| - |\n| b |");
  for (std::size_t pos : {3U, 15U}) {
    CHECK_FALSE(insert_line_break(doc, {pos, pos}, "\n"));
  }
}

TEST_CASE("commands: Shift+Enter elsewhere does its default") {
  CHECK(run(break_lf, "^a|") == "none");
  CHECK(run(break_lf, "|- a") == "none");
  CHECK(run(break_lf, "-| a") == "none");
  CHECK(run(break_lf, "> |- a") == "none");
  CHECK(run(break_lf, "- a\n | b") == "none");
  CHECK(run(break_lf, "1. |a") == "none");
  CHECK(run(break_lf, "- a\n  |b") == "none");
  CHECK(run(break_lf, "> |a") == "none");
  CHECK(run(break_lf, "a\n|") == "none");
}

TEST_CASE("commands: Shift+Enter writes the given line ending") {
  CHECK(run(break_crlf, "- a|") == "- a\\\r\n  |");
  CHECK(run(break_crlf, "a\r\nb|") == "a\r\nb\\\r\n|");
}

TEST_CASE("commands: Tab indents list items") {
  CHECK(run(indent_list, "- a\n- b|") == "- a\n  - b|");
  CHECK(run(indent_list, "- a\n|- b") == "- a\n  |- b");
  CHECK(run(indent_list, "1. a\n2. b|") == "1. a\n   1. b|");
  CHECK(run(indent_list, "- a\n- b|\n  - c") == "- a\n  - b|\n    - c");
  CHECK(run(indent_list, "- a\n- b|\n  c") == "- a\n  - b|\n    c");
  CHECK(run(indent_list, "- a\n- b|\n\n  c") == "- a\n  - b|\n\n    c");
  CHECK(run(indent_list, "- a\n- ^b\n- c|") == "- a\n  - ^b\n  - c|");
  CHECK(run(indent_list, "> - a\n> - b|") == "> - a\n>   - b|");
}

TEST_CASE("commands: Tab on a first item changes nothing") {
  CHECK(run(indent_list, "- a|") == "- a|");
  CHECK(run(indent_list, "- ^a\n- b|") == "- ^a\n- b|");
}

TEST_CASE("commands: Tab outside a list does its default") {
  CHECK(run(indent_list, "a|") == "none");
  CHECK(run(indent_list, "> a|") == "none");
}

TEST_CASE("commands: Shift+Tab outdents list items") {
  CHECK(run(outdent_list, "- a\n  - b|") == "- a\n- b|");
  CHECK(run(outdent_list, "- a\n  - b|\n    - c") == "- a\n- b|\n  - c");
  CHECK(run(outdent_list, "- a\n  - b|\n    c") == "- a\n- b|\n  c");
  CHECK(run(outdent_list, "1. a\n   - b|") == "1. a\n- b|");
  CHECK(run(outdent_list, "  - a|") == "- a|");
  CHECK(run(outdent_list, "> - a\n>   - b|") == "> - a\n> - b|");
  CHECK(run(outdent_list, "- a\n  - ^b\n  - c|") == "- a\n- ^b\n- c|");
}

TEST_CASE("commands: Shift+Tab on a top level item changes nothing") {
  CHECK(run(outdent_list, "- a|") == "- a|");
  CHECK(run(outdent_list, "a|") == "none");
}

TEST_CASE("commands: Backspace after a marker removes it") {
  CHECK(run(delete_markup_backward, "- |a") == "|a");
  CHECK(run(delete_markup_backward, "1. |a") == "|a");
  CHECK(run(delete_markup_backward, "- [ ] |a") == "|a");
  CHECK(run(delete_markup_backward, "- a\n- |b") == "- a\n  |b");
  CHECK(run(delete_markup_backward, "- a\n  |b") == "- a\n|b");
  CHECK(run(delete_markup_backward, "-   |a") == "- |a");
  CHECK(run(delete_markup_backward, "> |a") == "|a");
  CHECK(run(delete_markup_backward, "> > |a") == "> |a");
  CHECK(run(delete_markup_backward, "- a\n  - |b") == "- a\n  |b");
}

TEST_CASE("commands: Backspace elsewhere does its default") {
  CHECK(run(delete_markup_backward, "- a|") == "none");
  CHECK(run(delete_markup_backward, "a|") == "none");
  CHECK(run(delete_markup_backward, "|- a") == "none");
  CHECK(run(delete_markup_backward, "- ^a|") == "none");
  CHECK(run(delete_markup_backward, "```\n- |x\n```") == "none");
}

namespace {

std::string renumbered(const std::string& text, const std::vector<Edit>& edits) {
  Document doc(text);
  const TextRegion region = merge_edits(edits);
  const ListShape before = list_shape(doc, region.start, region.old_end);
  doc.begin();
  for (const Edit& e : edits) doc.apply(e);
  if (const auto r = renumber_lists(doc, before, region.start, region.new_end, {})) {
    for (const Edit& e : r->edits) doc.apply(e);
  }
  doc.commit();
  return doc.text();
}

std::string renumbered(Command cmd, const std::string& marked) {
  std::string text = marked;
  const std::size_t pos = text.find('|');
  text.erase(pos, 1);
  const Document doc(text);
  const auto r = cmd(doc, {pos, pos});
  REQUIRE(r);
  return renumbered(text, r->edits);
}

}  

TEST_CASE("commands: renumber after a changed item count") {
  const std::string abc = "1. a\n2. b\n3. c";
  CHECK(renumbered(abc, {Edit{5, 5, ""}}) == "1. a\n2. c");
  CHECK(renumbered(abc, {Edit{0, 5, ""}}) == "1. b\n2. c");
  CHECK(renumbered(abc, {Edit{6, 1, ""}}) == "1. a\n2 b\n2. c");
  CHECK(renumbered("1. a\n2. b", {Edit{5, 0, "1. x\n2. y\n"}}) == "1. a\n2. x\n3. y\n4. b");
  CHECK(renumbered("1. a\n2. b\n3. c\n7. d", {Edit{5, 5, ""}}) == "1. a\n2. c\n7. d");
  CHECK(renumbered("8. a\n9. b\n   - p", {Edit{5, 0, "1. x\n"}}) == "8. a\n9. x\n10. b\n    - p");
  CHECK(renumbered("3. a\n4. b", {Edit{0, 5, ""}}) == "3. b");
  CHECK(renumbered("9. a", {Edit{0, 0, "1. x\n"}}) == "1. x\n2. a");
  CHECK(renumbered("p\n3. a\n4. b", {Edit{1, 0, "\n1. x"}}) == "p\n1. x\n2. a\n3. b");
  CHECK(renumbered("1. a\n   1. x\n   2. y\n   3. z\n2. b", {Edit{13, 8, ""}}) ==
        "1. a\n   1. x\n   2. z\n2. b");
}

TEST_CASE("commands: renumber after a marker comes back") {
  CHECK(renumbered("1. a\n2 b\n2. c\n3. d", {Edit{6, 0, "."}}) == "1. a\n2. b\n3. c\n4. d");
  CHECK(renumbered("1. a\n2. b\n. c\n3. d", {Edit{10, 0, "7"}}) == "1. a\n2. b\n3. c\n4. d");
  CHECK(renumbered("", {Edit{0, 0, "1. a\n2. b\n7. c\n8. z"}}) == "1. a\n2. b\n3. c\n4. z");
  CHECK(renumbered("p\n\n", {Edit{3, 0, "1. a\n7. c\n"}}) == "p\n\n1. a\n2. c\n");
}

TEST_CASE("commands: renumber after an edit of the digits") {
  const std::string abc = "1. a\n2. b\n3. c";
  CHECK(renumbered(abc, {Edit{5, 1, "5"}}) == abc);
  CHECK(renumbered(abc, {Edit{5, 0, "1"}}) == abc);
  CHECK(renumbered("1. a\n12. b\n3. c", {Edit{5, 1, ""}}) == abc);
  CHECK(renumbered(abc, {Edit{0, 1, "5"}}) == "5. a\n6. b\n7. c");
  CHECK(renumbered(abc, {Edit{0, 0, "1"}}) == "11. a\n12. b\n13. c");
  CHECK(renumbered("1. a\n   1. x\n   2. y\n2. b", {Edit{16, 1, "7"}}) == "1. a\n   1. x\n   2. y\n2. b");
}

TEST_CASE("commands: renumber keeps lists without a count change") {
  const std::string abc = "1. a\n2. b\n3. c";
  CHECK(renumbered(abc, {Edit{9, 0, "x\ny"}}) == "1. a\n2. bx\ny\n3. c");
  CHECK(renumbered("5", {Edit{1, 0, "."}}) == "5.");
  CHECK(renumbered("1. a\n1. b\n1. c", {Edit{5, 5, ""}}) == "1. a\n1. c");
  CHECK(renumbered("1. a\n1. b", {Edit{5, 0, "7. x\n"}}) == "1. a\n1. x\n1. b");
  CHECK(renumbered(enter_lf, "1. one|\n1. three") == "1. one\n1. \n1. three");
}

namespace {

std::vector<std::string> out_of_sequence_text(const std::string& text) {
  const Document doc(text);
  std::vector<std::string> out;
  for (const ByteRange& r : out_of_sequence(doc)) out.push_back(text.substr(r.start, r.end - r.start));
  return out;
}

}  

TEST_CASE("commands: out of sequence items") {
  using V = std::vector<std::string>;
  CHECK(out_of_sequence_text("1. a\n2. b\n3. c") == V{});
  CHECK(out_of_sequence_text("1. a\n2. b\n5. c\n6. d") == V{"5"});
  CHECK(out_of_sequence_text("3. a\n4. b") == V{});
  CHECK(out_of_sequence_text("1. a\n1. b\n1. c") == V{});
  CHECK(out_of_sequence_text("1. a\n1. b\n2. c") == V{"1"});
  CHECK(out_of_sequence_text("10) a\n12) b") == V{"12"});
  CHECK(out_of_sequence_text("1. a\n   1. x\n   3. y\n2. b") == V{"3"});
  CHECK(out_of_sequence_text("> 1. a\n> 7. b") == V{"7"});
}

TEST_CASE("commands: renumber the lists in the selection") {
  CHECK(run(renumber, "1. a\n5. b|\n9. c") == "1. a\n2. b|\n3. c");
  CHECK(run(renumber, "3. a|\n3. b") == "3. a|\n4. b");
  CHECK(run(renumber, "1. a|\n1. b\n1. c") == "1. a|\n2. b\n3. c");
  CHECK(run(renumber, "^1. a\n   1. x\n   9. y\n7. b|") == "^1. a\n   1. x\n   2. y\n2. b|");
  CHECK(run(renumber, "1. a\n   1. x|\n   5. y\n7. b") == "1. a\n   1. x|\n   2. y\n7. b");
  CHECK(run(renumber, "9. a|\n9. b\n   - c") == "9. a|\n10. b\n    - c");
  CHECK(run(renumber, "- a|") == "none");
  CHECK(run(renumber, "1. a|\n2. b") == "1. a|\n2. b");
}

TEST_CASE("commands: make a marker text") {
  CHECK(run(marker_to_text, "1. a\n5|. b") == "1. a\n5\\|. b");
  CHECK(run(marker_to_text, "1. a\n2) |b") == "1. a\n2\\) |b");
  CHECK(run(marker_to_text, "- a|") == "none");
  CHECK(run(marker_to_text, "a|") == "none");
}

TEST_CASE("commands: renumber after Tab and Shift+Tab") {
  CHECK(run(indent_list, "1. a\n2. b|\n3. c") == "1. a\n   1. b|\n3. c");
  CHECK(run(indent_list, "9. a\n10. b|") == "9. a\n   1. b|");
  CHECK(run(indent_list, "9. a\n10. b|\n    c") == "9. a\n   1. b|\n      c");
  CHECK(run(indent_list, "1. x\n10000. y|\n       z") == "1. x\n   1. y|\n      z");
  CHECK(renumbered(indent_list, "1. a\n2. b|\n3. c") == "1. a\n   1. b\n2. c");
  CHECK(renumbered(indent_list, "1. a\n   1. x\n2. b|\n3. c") == "1. a\n   1. x\n   2. b\n2. c");
  CHECK(renumbered(outdent_list, "1. a\n   1. b\n   2. c|\n2. d") == "1. a\n   1. b\n2. c\n3. d");
  CHECK(run(outdent_list, "1. a\n   1. b|\n   2. c") == "1. a\n1. b|\n   1. c");
  CHECK(renumbered(outdent_list, "1. a\n   1. b|\n   2. c\n   3. d\n2. e") ==
        "1. a\n2. b\n   1. c\n   2. d\n3. e");
}

namespace {

std::optional<CommandResult> bold(const Document& d, Selection s) { return toggle_inline(d, s, InlineKind::strong); }
std::optional<CommandResult> italic(const Document& d, Selection s) {
  return toggle_inline(d, s, InlineKind::emphasis);
}
std::optional<CommandResult> strike(const Document& d, Selection s) {
  return toggle_inline(d, s, InlineKind::strikethrough);
}
std::optional<CommandResult> code(const Document& d, Selection s) { return toggle_inline(d, s, InlineKind::code); }
auto link_cmd(std::string text, std::string url) {
  return [text, url](const Document& d, Selection s) { return insert_link(d, s, text, url); };
}
auto image_cmd(std::string alt, std::string url) {
  return [alt, url](const Document& d, Selection s) { return insert_image(d, s, alt, url); };
}

Format format_of(const std::string& marked) {
  std::string text;
  std::size_t cursor = 0;
  std::size_t anchor = std::string::npos;
  for (char c : marked) {
    if (c == '|') {
      cursor = text.size();
    } else if (c == '^') {
      anchor = text.size();
    } else {
      text += c;
    }
  }
  if (anchor == std::string::npos) anchor = cursor;
  Document doc(text);
  return format_at(doc, {anchor, cursor});
}

}  

TEST_CASE("commands: bold wraps the selection") {
  CHECK(run(bold, "^a|") == "**^a|**");
  CHECK(run(bold, "x ^a b| y") == "x **^a b|** y");
  CHECK(run(bold, "x| a ^y") == "x **|a^** y");
  CHECK(run(bold, "^ a |") == " **^a|** ");
  CHECK(run(bold, "^a\nb|") == "**^a\nb|**");
  CHECK(run(bold, "# ^a|") == "# **^a|**");
  CHECK(run(bold, "- ^a|") == "- **^a|**");
  CHECK(run(bold, "*a b| c*") == "*a **b|** c*");
}

TEST_CASE("commands: bold removes the markers of a strong node") {
  CHECK(run(bold, "**^a|**") == "^a|");
  CHECK(run(bold, "^**a**|") == "^a|");
  CHECK(run(bold, "**a|**") == "a|");
  CHECK(run(bold, "**|a**") == "|a");
  CHECK(run(bold, "x **a b|c** y") == "x a b|c y");
  CHECK(run(bold, "__a|__") == "a|");
  CHECK(run(bold, "*a **b|** c*") == "*a b| c*");
  CHECK(run(bold, "***a|***") == "*a|*");
  CHECK(run(bold, "**a\nb|**") == "a\nb|");
}

TEST_CASE("commands: bold at an empty cursor") {
  CHECK(run(bold, "fo|o") == "**fo|o**");
  CHECK(run(bold, "foo| bar") == "**foo|** bar");
  CHECK(run(bold, "foo |") == "foo **|**");
  CHECK(run(bold, "|") == "**|**");
  CHECK(run(bold, "abc\n|\n") == "abc\n**|**\n");
  CHECK(run(bold, "a\n\n|\n\nb") == "a\n\n**|**\n\nb");
  CHECK(run(bold, "a\r\n\r\n|\r\n") == "a\r\n\r\n**|**\r\n");
  CHECK(run(italic, "a\n\n|\n") == "a\n\n*|*\n");
  CHECK(run(italic, "abc\n|") == "abc\n*|*");
  CHECK(run(bold, "**|**") == "|");
  CHECK(run(bold, "a **|** b") == "a | b");
  CHECK(run(bold, "caf|é") == "**caf|é**");
}

TEST_CASE("commands: bold does not apply over blocks or in code") {
  CHECK(run(bold, "^a\n\nb|") == "none");
  CHECK(run(bold, "- ^a\n- b|") == "none");
  CHECK(run(bold, "`a ^b| c`") == "none");
  CHECK(run(bold, "```\n^a|\n```") == "none");
}

TEST_CASE("commands: italic and strikethrough") {
  CHECK(run(italic, "^a|") == "*^a|*");
  CHECK(run(italic, "*a|*") == "a|");
  CHECK(run(italic, "_a|_") == "a|");
  CHECK(run(italic, "***a|***") == "**a|**");
  CHECK(run(italic, "|") == "*|*");
  CHECK(run(italic, "*|*") == "|");
  CHECK(run(strike, "^a|") == "~~^a|~~");
  CHECK(run(strike, "~~a|~~") == "a|");
  CHECK(run(strike, "~a|~") == "a|");
}

TEST_CASE("commands: code span toggle") {
  CHECK(run(code, "^a|") == "`^a|`");
  CHECK(run(code, "^a`b|") == "``^a`b|``");
  CHECK(run(code, "^`a|") == "`` ^`a| ``");
  CHECK(run(code, "`a|`") == "a|");
  CHECK(run(code, "``a`b|``") == "a`b|");
  CHECK(run(code, "`` `a| ``") == "`a|");
  CHECK(run(code, "**^a|**") == "**`^a|`**");
  CHECK(run(code, "|") == "`|`");
  CHECK(run(code, "`|`") == "|");
}

TEST_CASE("commands: insert a link") {
  CHECK(run(link_cmd("a", "u"), "^a|") == "[a](u)|");
  CHECK(run(link_cmd("a", "u"), "x ^a| y") == "x [a](u)| y");
  CHECK(run(link_cmd("a", "u"), "^ a |") == " [a](u)| ");
  CHECK(run(link_cmd("new", "u"), "^a|") == "[new](u)|");
  CHECK(run(link_cmd("", "u"), "|") == "[|](u)");
  CHECK(run(link_cmd("t", "u"), "x |") == "x [t](u)|");
  CHECK(run(link_cmd("t", "u"), "|") == "[t](u)|");
  CHECK(run(link_cmd("t", "u"), "a\n|\n") == "a\n[t](u)|\n");
  CHECK(run(link_cmd("a", ""), "^a|") == "[a](|)");
  CHECK(run(link_cmd("a", "a b"), "^a|") == "[a](<a b>)|");
  CHECK(run(link_cmd("a", "x(y"), "^a|") == "[a](<x(y>)|");
  CHECK(run(link_cmd("a]b", "u"), "|") == "[a\\]b](u)|");
  CHECK(run(link_cmd("[x] y", "u"), "|") == "[[x] y](u)|");
  CHECK(run(link_cmd("**a**", "u"), "^**a**|") == "[**a**](u)|");
}

TEST_CASE("commands: insert a link does not apply here") {
  CHECK(run(link_cmd("t", "u"), "^a\n\nb|") == "none");
  CHECK(run(link_cmd("t", "u"), "`^a|`") == "none");
  CHECK(run(link_cmd("t", "u"), "[a|](u)") == "none");
  CHECK(run(link_cmd("t", "u"), "!|") == "none");
}

TEST_CASE("commands: insert an image") {
  CHECK(run(image_cmd("a", "u.png"), "^a|") == "![a](u.png)|");
  CHECK(run(image_cmd("a", "u"), "x ^a| y") == "x ![a](u)| y");
  CHECK(run(image_cmd("", "u"), "|") == "![|](u)");
  CHECK(run(image_cmd("a", ""), "^a|") == "![a](|)");
  CHECK(run(image_cmd("t", "u"), "x |") == "x ![t](u)|");
  CHECK(run(image_cmd("t", "a b.png"), "|") == "![t](<a b.png>)|");
  CHECK(run(image_cmd("t", "x(1).png"), "|") == "![t](<x(1).png>)|");
  CHECK(run(image_cmd("a]b", "u"), "|") == "![a\\]b](u)|");
  CHECK(run(image_cmd("t", "u"), "Above.\n|\nBelow.") == "Above.\n![t](u)|\nBelow.");
  CHECK(run(image_cmd("t", "u"), "> a\n> |") == "> a\n> ![t](u)|");
  CHECK(run(image_cmd("t", "u"), "[a |](v)") == "[a ![t](u)|](v)");
}

TEST_CASE("commands: insert an image does not apply here") {
  CHECK(run(image_cmd("t", "u"), "^a\n\nb|") == "none");
  CHECK(run(image_cmd("t", "u"), "`^a|`") == "none");
  CHECK(run(image_cmd("t", "u"), "\\|") == "none");
}

TEST_CASE("commands: remove a link") {
  CHECK(run(remove_link, "[a|](u)") == "a|");
  CHECK(run(remove_link, "^[a](u)|") == "^a|");
  CHECK(run(remove_link, "[|](u)") == "|");
  CHECK(run(remove_link, "[a **b**|](u \"t\")") == "a **b**|");
  CHECK(run(remove_link, "[a|][r]\n\n[r]: v") == "a|\n\n[r]: v");
  CHECK(run(remove_link, "a|") == "none");
  CHECK(run(remove_link, "[a](u)|") == "none");
}

TEST_CASE("commands: format at the selection") {
  Format f;
  f.strong = true;
  CHECK(format_of("**a|**") == f);
  CHECK(format_of("**|a**") == f);
  CHECK(format_of("^**a**|") == f);
  CHECK(format_of("**a**|") == Format{});
  CHECK(format_of("|**a**") == Format{});
  CHECK(format_of("**a**| b") == Format{});
  f.emphasis = true;
  CHECK(format_of("*a **b|** c*") == f);
  f = {};
  f.emphasis = true;
  CHECK(format_of("*a **b** c|*") == f);
  CHECK(format_of("*^a **b** c|*") == f);
  f = {};
  f.code = true;
  CHECK(format_of("`a|`") == f);
  f = {};
  f.strikethrough = true;
  CHECK(format_of("~~a|~~") == f);
  f = {};
  f.link = true;
  CHECK(format_of("[a|](u)") == f);
  f = {};
  f.heading_level = 2;
  CHECK(format_of("## a|") == f);
  CHECK(format_of("a|\n--") == f);
  f = {};
  f.list = ListKind::bullet;
  f.task = true;
  CHECK(format_of("- [ ] a|") == f);
  f.task = false;
  f.list = ListKind::ordered;
  CHECK(format_of("- a\n  1. b|") == f);
  f = {};
  f.block_quote = true;
  f.list = ListKind::bullet;
  f.strong = true;
  CHECK(format_of("> - **a|**") == f);
  f = {};
  f.table = true;
  {
    const Document table("| a |\n| - |\n| b |");
    CHECK(format_at(table, {15, 15}) == f);
  }
  f = {};
  f.heading_level = 1;
  CHECK(format_of("# a|\nb") == f);
  CHECK(format_of("# a\n|b") == Format{});
  f = {};
  f.code_block = true;
  CHECK(format_of("```\nx|\n```") == f);
  CHECK(format_of("    x|") == f);
  f.list = ListKind::bullet;
  CHECK(format_of("- ```\n  x|\n  ```") == f);
}

namespace {

std::optional<CommandResult> h0(const Document& d, Selection s) { return set_heading(d, s, 0); }
std::optional<CommandResult> h1(const Document& d, Selection s) { return set_heading(d, s, 1); }
std::optional<CommandResult> h2(const Document& d, Selection s) { return set_heading(d, s, 2); }
std::optional<CommandResult> h3(const Document& d, Selection s) { return set_heading(d, s, 3); }

std::optional<CommandResult> bullet(const Document& d, Selection s) {
  return toggle_block(d, s, BlockKind::bullet_list);
}
std::optional<CommandResult> ordered(const Document& d, Selection s) {
  return toggle_block(d, s, BlockKind::ordered_list);
}
std::optional<CommandResult> task(const Document& d, Selection s) { return toggle_block(d, s, BlockKind::task_list); }
std::optional<CommandResult> quote(const Document& d, Selection s) {
  return toggle_block(d, s, BlockKind::block_quote);
}
std::optional<CommandResult> code_blk(const Document& d, Selection s) {
  return toggle_block(d, s, BlockKind::code_block);
}
std::optional<CommandResult> hr(const Document& d, Selection s) { return insert_thematic_break(d, s); }

}  

TEST_CASE("commands: set_heading") {
  CHECK(run(h2, "a|b") == "## a|b");
  CHECK(run(h0, "## a|") == "a|");
  CHECK(run(h3, "# a|") == "### a|");
  CHECK(run(h1, "- a|") == "- # a|");
  CHECK(run(h1, "> a|") == "> # a|");
  CHECK(run(h2, "a|\n===\n") == "## a|\n");
  CHECK(run(h1, "x\ny|\nz") == "x\n# y|\nz");
  CHECK(run(h0, "# a|\nb") == "a|\n\nb");
  CHECK(run(h0, "x\n# a|") == "x\n\na|");
  CHECK(run(h1, "|") == "# |");
  CHECK(run(h1, "```\nc|\n```") == "none");
  CHECK(run(h1, "^a\nb|") == "# ^a\n# b|");
  CHECK(run(h1, "^a\nb\n|c") == "# ^a\n# b\n|c");
  CHECK(run(h2, "a|\r\n") == "## a|\r\n");
}

TEST_CASE("commands: toggle_block bullet_list and ordered_list") {
  CHECK(run(bullet, "a|") == "- a|");
  CHECK(run(bullet, "^a\nb|") == "- ^a\n- b|");
  CHECK(run(bullet, "- a|") == "a|");
  CHECK(run(bullet, "1. a|\n2. b") == "- a|\n- b");
  CHECK(run(bullet, "- a\n- b|\n- c") == "- a\n\nb|\n- c");
  CHECK(run(bullet, "* a\n\nb|") == "* a\n\n* b|");
  CHECK(run(ordered, "1. a\n\nb|") == "1. a\n\n2. b|");
  CHECK(run(bullet, "- a|\n  more") == "a|\nmore");
  CHECK(run(bullet, "a|\nb") == "- a|\n\nb");
  CHECK(run(bullet, "# h|") == "- # h|");
  CHECK(run(bullet, "a|\r\n") == "- a|\r\n");
}

TEST_CASE("commands: toggle_block task_list") {
  CHECK(run(task, "a|") == "- [ ] a|");
  CHECK(run(task, "- a|") == "- [ ] a|");
  CHECK(run(task, "- [x] a|") == "- a|");
  CHECK(run(task, "1. a|") == "1. [ ] a|");
  CHECK(run(task, "- a|", Dialect::commonmark()) == "none");
  CHECK(run(task, "a|\r\n") == "- [ ] a|\r\n");
}

TEST_CASE("commands: toggle_block block_quote") {
  CHECK(run(quote, "a|") == "> a|");
  CHECK(run(quote, "^a\n\nb|") == "> ^a\n>\n> b|");
  CHECK(run(quote, "> a\n> b|\n> c") == "a\nb|\nc");
  CHECK(run(quote, "> > a|") == "> a|");
  CHECK(run(quote, "- a|") == "> - a|");
  CHECK(run(quote, "- a\n  b|") == "- a\n  > b|");
  CHECK(run(quote, "x\n> a|") == "x\n\na|");
  CHECK(run(quote, "a|\nb") == "> a|\n\nb");
  CHECK(run(quote, "a|\r\n") == "> a|\r\n");
}

TEST_CASE("commands: toggle_block code_block") {
  CHECK(run(code_blk, "a|") == "```\na|\n```");
  CHECK(run(code_blk, "a|\n") == "```\na|\n```\n");
  CHECK(run(code_blk, "|") == "```\n|\n```");
  CHECK(run(code_blk, "```\na|\n```") == "a|");
  CHECK(run(code_blk, "x\n```\na|\n```\ny") == "x\n\na|\n\ny");
  CHECK(run(code_blk, "    a|") == "a|");
  CHECK(run(code_blk, "> a|") == "> ```\n> a|\n> ```");
  CHECK(run(code_blk, "a ```b```|") == "````\na ```b```|\n````");
  CHECK(run(code_blk, "a|\r\n") == "```\r\na|\r\n```\r\n");
}

TEST_CASE("commands: insert_thematic_break") {
  CHECK(run(hr, "a|") == "a\n\n---\n|");
  CHECK(run(hr, "a|\n\nb") == "a\n\n---\n\n|b");
  CHECK(run(hr, "a|\nb") == "a\nb\n\n---\n|");
  CHECK(run(hr, "- a|") == "- a\n\n  ---\n|");
  CHECK(run(hr, "```\nc|\n```") == "none");
  CHECK(run(hr, "a|\r\n") == "a\r\n\r\n---\r\n|");
  CHECK(run(hr, "# h|") == "# h\n\n---\n|");
  CHECK(run(hr, "|") == "---\n|");
  CHECK(run(hr, "a\n|") == "a\n\n---\n|");
  CHECK(run(hr, "a\n\n|") == "a\n\n---\n|");
  CHECK(run(hr, "a\n\n|\n\nb") == "a\n\n---\n|\nb");
  CHECK(run(hr, "a\r\n\r\n|") == "a\r\n\r\n---\r\n|");
  CHECK(run(hr, "> a\n>\n> |") == "> a\n>\n> ---\n|");
  CHECK(run(hr, "- a\n\n  |") == "- a\n\n  ---\n|");
  CHECK(run(hr, "- a\n- |") == "none");
  CHECK(run(hr, "```\n|\n```") == "none");
}

namespace {

std::string random_document(std::mt19937& rng) {
  static const std::vector<std::string> pieces = {
      "- a\n", "- b c\n", "  - d\n", "    - e\n", "1. f\n", "2. g\n", "   1. h\n", "- [ ] i\n", "- [x] j\n",
      "> k\n", "> - l\n", ">   - m\n", "> > n\n", ">\n", "\n", "para\n", "  cont\n", "```\n", "  code\n",
      "    ind\n", "- \n", "* o\n", "+ p\n", "10) q\n", "- > r\n", "\t- s\n", "- é\n", "#  t\n", "-\n"};
  std::string out;
  const int n = std::uniform_int_distribution<int>(1, 12)(rng);
  for (int i = 0; i < n; ++i) out += pieces[std::uniform_int_distribution<std::size_t>(0, pieces.size() - 1)(rng)];
  if (std::uniform_int_distribution<int>(0, 3)(rng) == 0) out.pop_back();
  return out;
}

bool boundary(const std::string& s, std::size_t i) {
  return i == s.size() || (static_cast<unsigned char>(s[i]) & 0xC0U) != 0x80U;
}

}  

TEST_CASE("commands: property check over random documents", "[random]") {
  std::mt19937 rng(12345);
  const Command commands[] = {enter_lf, enter_crlf, break_lf, break_crlf, indent_list, outdent_list,
                              delete_markup_backward};
  int applied = 0;
  for (int round = 0; round < 400; ++round) {
    const std::string text = random_document(rng);
    for (std::size_t pos = 0; pos <= text.size(); ++pos) {
      if (!boundary(text, pos)) continue;
      std::vector<Selection> sels = {{pos, pos}};
      const std::size_t other = std::uniform_int_distribution<std::size_t>(0, text.size())(rng);
      if (boundary(text, other)) sels.push_back({other, pos});
      for (const Selection sel : sels) {
        for (Command cmd : commands) {
          Document doc(text);
          const auto r = cmd(doc, sel);
          if (!r) continue;
          INFO("text: " << text << " anchor: " << sel.anchor << " cursor: " << sel.cursor);
          doc.begin();
          for (const Edit& e : r->edits) REQUIRE_NOTHROW(doc.apply(e));
          doc.commit();
          REQUIRE(r->selection.cursor <= doc.size());
          REQUIRE(r->selection.anchor <= doc.size());
          REQUIRE_FALSE(doc.tree().validate(doc.size()).has_value());
          if (!r->edits.empty()) {
            ++applied;
            const TextRegion region = merge_edits(r->edits);
            const Buffer before(text);
            const std::size_t lo = std::min(sel.anchor, sel.cursor);
            const std::size_t hi = std::max(sel.anchor, sel.cursor);
            std::size_t first_line = before.line_of(lo);
            first_line = first_line == 0 ? 0 : first_line - 1;
            std::size_t block_end = 0;
            const Document plain(text);
            const SyntaxTree& tree = plain.tree();
            for (NodeId cur = tree.node_at(lo); cur != no_node; cur = tree[cur].parent) {
              const NodeKind k = tree[cur].kind;
              if (k == NodeKind::list || k == NodeKind::block_quote) {
                first_line = std::min(first_line, before.line_of(tree[cur].start));
                block_end = std::max(block_end, tree[cur].end);
              }
            }
            const std::size_t from = before.line_start(first_line);
            CHECK(region.start >= from);
            if (sel.anchor == sel.cursor && cmd != indent_list && cmd != outdent_list) {
              const std::size_t line = before.line_of(hi);
              std::size_t end = line + 1 < before.line_count() ? before.line_start(line + 1) : before.size();
              if (cmd == enter_lf || cmd == enter_crlf) end = std::max(end, block_end);
              CHECK(region.old_end <= end);
            }
            doc.undo();
            REQUIRE(doc.text() == text);
          }
        }
      }
    }
  }
  CHECK(applied > 1000);
}

namespace {

std::string escaped(const std::string& t) {
  std::string out;
  for (char c : t) out += c == '\n' ? std::string("\\n") : c == '\r' ? std::string("\\r") : c == '\t' ? std::string("\\t") : std::string(1, c);
  return out;
}

std::vector<NodeKind> containers(const SyntaxTree& tree, NodeId id) {
  std::vector<NodeKind> out;
  for (NodeId cur = id; cur != no_node; cur = tree[cur].parent) {
    if (tree[cur].kind == NodeKind::list_item || tree[cur].kind == NodeKind::block_quote) {
      out.push_back(tree[cur].kind);
    }
  }
  return out;
}

}  

namespace {

std::vector<ListShape::Item> all_items(const Document& doc) { return list_shape(doc, 0, doc.size()).items; }

}  

TEST_CASE("commands: property check of renumbering over random documents", "[random]") {
  std::mt19937 rng(99);
  static const std::vector<std::string> pastes = {"1. x\n", "2. y\n3. z\n", "- w\n", "\n", "v", "   1. u\n", "9) t\n"};
  int renumbered_edits = 0;
  for (int round = 0; round < 12000; ++round) {
    const std::string text = random_document(rng);
    const auto pick = [&] {
      std::size_t p = std::uniform_int_distribution<std::size_t>(0, text.size())(rng);
      while (!boundary(text, p)) --p;
      return p;
    };
    std::size_t a = pick();
    std::size_t b = pick();
    if (a > b) std::swap(a, b);
    const int kind = std::uniform_int_distribution<int>(0, 2)(rng);
    const std::string paste = pastes[std::uniform_int_distribution<std::size_t>(0, pastes.size() - 1)(rng)];
    const Edit e{a, kind == 1 ? 0 : b - a, kind == 0 ? std::string() : paste};
    Document doc(text);
    INFO("text: " << escaped(text) << " edit: " << e.offset << " " << e.removed << " " << escaped(e.inserted));
    const ListShape before = list_shape(doc, e.offset, e.offset + e.removed);
    doc.begin();
    doc.apply(e);
    const std::string edited = doc.text();
    const auto shape = all_items(doc);
    if (const auto r = renumber_lists(doc, before, e.offset, e.offset + e.inserted.size(), {})) {
      for (const Edit& x : r->edits) REQUIRE_NOTHROW(doc.apply(x));
      const auto strip = [](std::string t) {
        std::erase_if(t, [](char c) { return std::string_view("0123456789.) ").find(c) != std::string_view::npos; });
        return t;
      };
      CHECK(strip(doc.text()) == strip(edited));
      ++renumbered_edits;
    }
    doc.commit();
    REQUIRE_FALSE(doc.tree().validate(doc.size()).has_value());
    CHECK(all_items(doc) == shape);
    doc.undo();
    REQUIRE(doc.text() == text);
  }
  CHECK(renumbered_edits > 200);
}

TEST_CASE("commands: property check of renumber over random documents", "[random]") {
  std::mt19937 rng(31337);
  static const std::vector<std::string> pieces = {"1. a\n", "7. b\n", "   3. c\n", "   1. d\n", "12) e\n", "- f\n",
                                                  "> 4. g\n", "\n", "para\n", "  cont\n", "9. h\n   - i\n"};
  int applied = 0;
  for (int round = 0; round < 3000; ++round) {
    std::string text;
    const int n = std::uniform_int_distribution<int>(1, 10)(rng);
    for (int i = 0; i < n; ++i) text += pieces[std::uniform_int_distribution<std::size_t>(0, pieces.size() - 1)(rng)];
    Document doc(text);
    INFO("text: " << escaped(text));
    const auto shape = all_items(doc);
    const auto r = renumber(doc, {0, text.size()});
    if (!r) continue;
    doc.begin();
    for (const Edit& e : r->edits) REQUIRE_NOTHROW(doc.apply(e));
    doc.commit();
    if (!r->edits.empty()) ++applied;
    REQUIRE_FALSE(doc.tree().validate(doc.size()).has_value());
    CHECK(all_items(doc) == shape);
    CHECK(out_of_sequence(doc).empty());
    if (!r->edits.empty()) {
      doc.undo();
      REQUIRE(doc.text() == text);
    }
  }
  CHECK(applied > 500);
}

namespace {

std::string random_list_document(std::mt19937& rng) {
  static const std::uint64_t starts[] = {1, 1, 8, 9, 98, 99, 998};
  const std::uint64_t start = starts[std::uniform_int_distribution<std::size_t>(0, 6)(rng)];
  const bool ordered = std::uniform_int_distribution<int>(0, 3)(rng) != 0;
  const int n = std::uniform_int_distribution<int>(2, 6)(rng);
  std::string out;
  int k = 0;
  for (int i = 0; i < n; ++i) {
    const std::string mark = ordered ? std::to_string(start + static_cast<std::uint64_t>(i)) + ". " : "- ";
    const std::string pad(mark.size(), ' ');
    const auto word = [&k] { return "i" + std::to_string(k++) + "\n"; };
    out += mark + word();
    switch (std::uniform_int_distribution<int>(0, 4)(rng)) {
      case 0: out += pad + "cont\n"; break;
      case 1:
        out += pad + "- " + word();
        out += pad + "- " + word();
        break;
      case 2:
        out += pad + "1. " + word();
        out += pad + "2. " + word();
        break;
      default: break;
    }
  }
  return out;
}

}  

TEST_CASE("commands: property check of Tab then Shift+Tab over random lists", "[random]") {
  std::mt19937 rng(2718);
  int round_trips = 0;
  for (int round = 0; round < 2000; ++round) {
    const std::string text = random_list_document(rng);
    for (int k = 0;; ++k) {
      const std::string word = "i" + std::to_string(k) + "\n";
      const std::size_t at = text.find(word);
      if (at == std::string::npos) break;
      INFO("text: " << escaped(text) << " item: " << k);
      const Document doc(text);
      const auto tab = indent_list(doc, {at, at});
      if (!tab || tab->edits.empty()) continue;
      const std::string indented = renumbered(text, tab->edits);
      const std::size_t moved = indented.find(word);
      REQUIRE(moved != std::string::npos);
      const Document doc2(indented);
      const auto shift_tab = outdent_list(doc2, {moved, moved});
      REQUIRE(shift_tab);
      CHECK(renumbered(indented, shift_tab->edits) == text);
      ++round_trips;
    }
  }
  CHECK(round_trips > 2000);
}

TEST_CASE("commands: property check of Shift+Enter over random documents", "[random]") {
  std::mt19937 rng(4242);
  int breaks = 0;
  for (int round = 0; round < 400; ++round) {
    const std::string text = random_document(rng);
    for (std::size_t pos = 1; pos <= text.size(); ++pos) {
      if (!boundary(text, pos) || text[pos - 1] == '\n') continue;
      const Document doc(text);
      const auto r = break_lf(doc, {pos, pos});
      if (!r || r->edits.empty()) continue;
      std::string marked = text;
      marked.insert(pos, "|");
      INFO("text: " << escaped(marked));
      Document after(text);
      after.begin();
      for (const Edit& e : r->edits) after.apply(e);
      after.apply(Edit{r->selection.cursor, 0, "x"});
      after.commit();
      CHECK(containers(after.tree(), after.tree().node_at(r->selection.cursor)) ==
            containers(doc.tree(), doc.tree().node_at(pos - 1)));
      const std::string& inserted = r->edits.front().inserted;
      if (!inserted.empty() && inserted.front() == '\\') {
        ++breaks;
        const NodeId hb = after.tree().node_at(r->edits.front().offset);
        CHECK(after.tree()[hb].kind == NodeKind::hard_break);
      }
    }
  }
  CHECK(breaks > 500);
}

namespace {

std::string random_inline_document(std::mt19937& rng) {
  static const std::vector<std::string> pieces = {
      "word ", "two words ", "**bold** ", "*it* ", "_un_ ", "__st__ ", "~~del~~ ", "`code` ", "``a`b`` ",
      "[link](u) ", "[ref][r] ", "***both*** ", "*a **b** c* ", "é ", "\n", "\n\n", "# ", "- ", "> ", "x**y**z ",
      "\"q\" ", "a_b_c ", "<http://a.b> ", "![img](i) ", "\\* ", "**", "*", "`"};
  std::string out;
  const int n = std::uniform_int_distribution<int>(1, 14)(rng);
  for (int i = 0; i < n; ++i) out += pieces[std::uniform_int_distribution<std::size_t>(0, pieces.size() - 1)(rng)];
  if (std::uniform_int_distribution<int>(0, 2)(rng) == 0) out += "\n\n[r]: v\n";
  return out;
}

bool format_has(const Format& f, int kind) {
  switch (kind) {
    case 0: return f.strong;
    case 1: return f.emphasis;
    case 2: return f.strikethrough;
    case 3: return f.code;
    case 4: return f.link;
    default: return false;  
  }
}

std::optional<CommandResult> toggle_kind(const Document& d, Selection s, int kind) {
  if (kind == 4) {
    if (format_at(d, s).link) return remove_link(d, s);
    const std::size_t lo = std::min(s.anchor, s.cursor);
    const std::size_t hi = std::max(s.anchor, s.cursor);
    return insert_link(d, s, d.buffer().slice(lo, hi - lo), "u");
  }
  if (kind == 5) {
    const std::size_t lo = std::min(s.anchor, s.cursor);
    const std::size_t hi = std::max(s.anchor, s.cursor);
    return insert_image(d, s, d.buffer().slice(lo, hi - lo), "u");
  }
  return toggle_inline(d, s, static_cast<InlineKind>(kind));
}

}  

TEST_CASE("commands: property check of the toggles over random documents", "[random]") {
  std::mt19937 rng(777);
  int round_trips = 0;
  for (int round = 0; round < 3000; ++round) {
    const std::string text = random_inline_document(rng);
    std::size_t a = std::uniform_int_distribution<std::size_t>(0, text.size())(rng);
    std::size_t c = std::uniform_int_distribution<std::size_t>(0, text.size())(rng);
    if (std::uniform_int_distribution<int>(0, 2)(rng) == 0) a = c;
    if (!boundary(text, a) || !boundary(text, c)) continue;
    const Selection sel{a, c};
    for (int kind = 0; kind < 6; ++kind) {
      Document doc(text);
      const bool before = format_has(format_at(doc, sel), kind);
      const auto r = toggle_kind(doc, sel, kind);
      if (!r) continue;
      INFO("text: " << text << " anchor: " << a << " cursor: " << c << " kind: " << kind);
      doc.begin();
      for (const Edit& e : r->edits) REQUIRE_NOTHROW(doc.apply(e));
      doc.commit();
      REQUIRE(r->selection.cursor <= doc.size());
      REQUIRE(r->selection.anchor <= doc.size());
      REQUIRE_FALSE(doc.tree().validate(doc.size()).has_value());
      const bool inserts = std::all_of(r->edits.begin(), r->edits.end(), [](const Edit& e) { return e.removed == 0; });
      if (!before && inserts && format_has(format_at(doc, r->selection), kind)) {
        const auto back = toggle_kind(doc, r->selection, kind);
        REQUIRE(back.has_value());
        Document again(doc.text());
        again.begin();
        for (const Edit& e : back->edits) again.apply(e);
        again.commit();
        CHECK(again.text() == text);
        ++round_trips;
      }
      if (!r->edits.empty()) {
        doc.undo();
        REQUIRE(doc.text() == text);
      }
    }
  }
  CHECK(round_trips > 1000);
}

namespace {

std::string random_block_document(std::mt19937& rng) {
  static const std::vector<std::string> pieces = {
      "- a\n", "- b c\n", "  - d\n", "    - e\n", "1. f\n", "2. g\n", "   1. h\n", "- [ ] i\n", "- [x] j\n",
      "> k\n", "> - l\n", ">   - m\n", "> > n\n", ">\n", "\n", "para\n", "  cont\n", "```\n", "  code\n",
      "    ind\n", "- \n", "* o\n", "+ p\n", "10) q\n", "- > r\n", "\t- s\n", "- é\n", "#  t\n", "-\n",
      "# h\n", "## h\n", "setext\n", "===\n", "---\n", "| a | b |\n", "|---|---|\n", "<div>\n", "\r\n"};
  std::string out;
  const int n = std::uniform_int_distribution<int>(1, 12)(rng);
  for (int i = 0; i < n; ++i) out += pieces[std::uniform_int_distribution<std::size_t>(0, pieces.size() - 1)(rng)];
  if (std::uniform_int_distribution<int>(0, 3)(rng) == 0) out.pop_back();
  return out;
}

bool is_leaf_kind(NodeKind k) {
  return k == NodeKind::paragraph || k == NodeKind::heading || k == NodeKind::code_block ||
         k == NodeKind::thematic_break || k == NodeKind::table || k == NodeKind::html_block;
}

std::vector<NodeKind> container_chain(const SyntaxTree& tree, NodeId id) {
  std::vector<NodeKind> chain;
  for (NodeId cur = tree[id].parent; cur != no_node; cur = tree[cur].parent) chain.push_back(tree[cur].kind);
  return chain;
}

void check_leaves_preserved(const std::string& old_text, const std::string& new_text,
                            const std::vector<Edit>& edits) {
  const Document before(old_text);
  const Document after(new_text);
  const Buffer& buf = before.buffer();
  const SyntaxTree& ot = before.tree();
  const SyntaxTree& nt = after.tree();
  const TextRegion region = merge_edits(edits);
  const std::ptrdiff_t delta =
      static_cast<std::ptrdiff_t>(region.new_end) - static_cast<std::ptrdiff_t>(region.old_end);
  std::size_t touch_start = old_text.size();
  std::size_t touch_end = 0;
  for (const Edit& e : edits) {
    touch_start = std::min(touch_start, buf.line_start(buf.line_of(e.offset)));
    const std::size_t re = e.offset + e.removed;
    const std::size_t end_line = buf.line_of(re);
    touch_end = std::max(touch_end, end_line + 1 < buf.line_count() ? buf.line_start(end_line + 1) : buf.size());
  }
  for (bool grew = true; grew;) {
    grew = false;
    for (const Node& n : ot.nodes()) {
      if (n.kind == NodeKind::document) continue;  
      if (n.start < touch_end && n.end > touch_start && (n.start < touch_start || n.end > touch_end)) {
        touch_start = std::min(touch_start, n.start);
        touch_end = std::max(touch_end, n.end);
        grew = true;
      }
    }
  }
  for (NodeId id = 0; id < ot.size(); ++id) {
    const Node& n = ot[id];
    if (!is_leaf_kind(n.kind)) continue;
    const bool before_region = n.end <= touch_start;
    const bool after_region = n.start >= touch_end;
    if (!before_region && !after_region) continue;
    const std::size_t ns = before_region ? n.start : static_cast<std::size_t>(static_cast<std::ptrdiff_t>(n.start) + delta);
    const std::size_t ne = before_region ? n.end : static_cast<std::size_t>(static_cast<std::ptrdiff_t>(n.end) + delta);
    bool found = false;
    for (NodeId nid = 0; nid < nt.size(); ++nid) {
      const Node& m = nt[nid];
      if (m.kind == n.kind && m.start == ns && m.end == ne) {
        found = true;
        CHECK(container_chain(ot, id) == container_chain(nt, nid));
        break;
      }
    }
    CHECK(found);
  }
}

template <typename Cmd>
void check_block_command(const char* name, const std::string& text, Selection sel, Cmd cmd, int* applied) {
  Document doc(text);
  const auto r = cmd(doc, sel);
  if (!r) return;
  INFO(name << " text: " << escaped(text) << " anchor: " << sel.anchor << " cursor: " << sel.cursor);
  doc.begin();
  for (const Edit& e : r->edits) REQUIRE_NOTHROW(doc.apply(e));
  doc.commit();
  REQUIRE(r->selection.cursor <= doc.size());
  REQUIRE(r->selection.anchor <= doc.size());
  REQUIRE_FALSE(doc.tree().validate(doc.size()).has_value());
  if (!r->edits.empty()) {
    if (applied) ++*applied;
    check_leaves_preserved(text, doc.text(), r->edits);
    doc.undo();
    REQUIRE(doc.text() == text);
  }
}

bool blank_line(const Buffer& buf, std::size_t line) {
  if (line >= buf.line_count()) return true;
  const std::size_t start = buf.line_start(line);
  const std::size_t end = line + 1 < buf.line_count() ? buf.line_start(line + 1) : buf.size();
  for (std::size_t i = start; i < end; ++i) {
    const char c = buf.at(i);
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
  }
  return true;
}

void check_block_round_trip(const std::string& text, Selection sel, BlockKind kind, int* round_trips) {
  if (kind != BlockKind::bullet_list && kind != BlockKind::block_quote && kind != BlockKind::code_block) return;
  if (sel.anchor != sel.cursor) return;
  Document doc(text);
  const SyntaxTree& tree = doc.tree();
  const Buffer& buf = doc.buffer();
  NodeId para = tree.node_at(sel.cursor);
  while (para != no_node && tree[para].kind != NodeKind::paragraph) para = tree[para].parent;
  if (para == no_node || tree[tree[para].parent].kind != NodeKind::document) return;
  const std::size_t line = buf.line_of(tree[para].start);
  if (buf.line_of(tree[para].end - 1) != line || buf.line_of(sel.cursor) != line) return;
  if ((line > 0 && !blank_line(buf, line - 1)) || !blank_line(buf, line + 1)) return;
  INFO("text: " << escaped(text) << " kind: " << static_cast<int>(kind) << " cursor: " << sel.cursor);
  const auto r = toggle_block(doc, sel, kind);
  REQUIRE(r);
  REQUIRE_FALSE(r->edits.empty());
  doc.begin();
  for (const Edit& e : r->edits) doc.apply(e);
  doc.commit();
  const auto back = toggle_block(doc, r->selection, kind);
  REQUIRE(back);
  Document again(doc.text());
  again.begin();
  for (const Edit& e : back->edits) again.apply(e);
  again.commit();
  CHECK(again.text() == text);
  if (round_trips) ++*round_trips;
}

}  

TEST_CASE("commands: property check of the block commands over random documents", "[random]") {
  std::mt19937 rng(2024);
  int applied = 0;
  int round_trips = 0;
  int refused = 0;
  lumos::set_log_level(lumos::LogLevel::debug);
  lumos::set_log_sink([&](lumos::LogLevel, std::string_view line) {
    if (line.find("kind=changes_outside") != std::string_view::npos) ++refused;
  });
  const BlockKind kinds[] = {BlockKind::bullet_list, BlockKind::ordered_list, BlockKind::task_list,
                             BlockKind::block_quote, BlockKind::code_block};
  for (int round = 0; round < 120; ++round) {
    const std::string text = random_block_document(rng);
    for (std::size_t pos = 0; pos <= text.size(); ++pos) {
      if (!boundary(text, pos)) continue;
      std::vector<Selection> sels = {{pos, pos}};
      const std::size_t other = std::uniform_int_distribution<std::size_t>(0, text.size())(rng);
      if (boundary(text, other) && other != pos) sels.push_back({other, pos});
      for (const Selection sel : sels) {
        for (const int level : {0, 1, 3}) {
          check_block_command("heading", text, sel, [level](const Document& d, Selection s) { return set_heading(d, s, level); },
                              &applied);
        }
        for (const BlockKind kind : kinds) {
          check_block_command("toggle_block", text, sel, [kind](const Document& d, Selection s) { return toggle_block(d, s, kind); },
                              &applied);
          check_block_round_trip(text, sel, kind, &round_trips);
        }
        check_block_command("break", text, sel, insert_thematic_break, &applied);
      }
    }
  }
  lumos::set_log_sink(nullptr);
  lumos::set_log_level(lumos::LogLevel::info);
  WARN("block commands: " << applied << " applied, " << refused << " refused, " << round_trips << " round trips");
  CHECK(applied > 500);
  CHECK(refused * 5 < applied);
  CHECK(round_trips > 20);
}

TEST_CASE("commands: block commands on every line of the sample note") {
  std::ifstream in(LUMOS_TEST_DATA_DIR "/sample.md", std::ios::binary);
  REQUIRE(in);
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string text = ss.str();
  int refused = 0;
  int applied = 0;
  lumos::set_log_level(lumos::LogLevel::debug);
  lumos::set_log_sink([&](lumos::LogLevel, std::string_view line) {
    if (line.find("kind=changes_outside") != std::string_view::npos) ++refused;
  });
  const Buffer buf(text);
  for (std::size_t line = 0; line < buf.line_count(); ++line) {
    const std::size_t p = buf.line_start(line);
    const Selection sel{p, p};
    const std::size_t e = line + 1 < buf.line_count() ? buf.line_start(line + 1) : buf.size();
    if (buf.slice(p, e - p).find_first_not_of(" \t\r\n>") == std::string::npos) continue;
    for (const int level : {0, 1, 3}) {
      check_block_command("heading", text, sel, [level](const Document& d, Selection s) { return set_heading(d, s, level); },
                          &applied);
    }
    for (const BlockKind kind : {BlockKind::bullet_list, BlockKind::ordered_list, BlockKind::task_list,
                                 BlockKind::block_quote, BlockKind::code_block}) {
      check_block_command("toggle_block", text, sel,
                          [kind](const Document& d, Selection s) { return toggle_block(d, s, kind); }, &applied);
    }
    check_block_command("break", text, sel, insert_thematic_break, &applied);
  }
  lumos::set_log_sink(nullptr);
  lumos::set_log_level(lumos::LogLevel::info);
  CHECK(applied > 150);
  CHECK(refused == 0);
}

TEST_CASE("commands: drop_text moves and copies text") {
  {
    Document doc("abcXYZdef");
    const auto r = drop_text(doc, {3, 6}, 9, true);
    REQUIRE(r);
    doc.begin();
    for (const Edit& e : r->edits) doc.apply(e);
    doc.commit();
    CHECK(doc.text() == "abcdefXYZ");
    CHECK(r->selection == Selection{6, 9});
    doc.undo();
    CHECK(doc.text() == "abcXYZdef");
  }
  {
    Document doc("abcdefXYZ");
    const auto r = drop_text(doc, {6, 9}, 3, true);
    REQUIRE(r);
    doc.begin();
    for (const Edit& e : r->edits) doc.apply(e);
    doc.commit();
    CHECK(doc.text() == "abcXYZdef");
    CHECK(r->selection == Selection{3, 6});
    doc.undo();
    CHECK(doc.text() == "abcdefXYZ");
  }
  {
    Document doc("abcXYZdef");
    CHECK_FALSE(drop_text(doc, {3, 6}, 3, true));
    CHECK_FALSE(drop_text(doc, {3, 6}, 6, true));
    CHECK_FALSE(drop_text(doc, {3, 6}, 4, true));
  }
  {
    Document doc("abcXYZdef");
    const auto r = drop_text(doc, {3, 6}, 9, false);
    REQUIRE(r);
    doc.begin();
    for (const Edit& e : r->edits) doc.apply(e);
    doc.commit();
    CHECK(doc.text() == "abcXYZdefXYZ");
    CHECK(r->selection == Selection{9, 12});
    doc.undo();
    CHECK(doc.text() == "abcXYZdef");
  }
  {
    Document doc("abcXYZdef");
    const auto r = drop_text(doc, {3, 6}, 4, false);
    REQUIRE(r);
    doc.begin();
    for (const Edit& e : r->edits) doc.apply(e);
    doc.commit();
    CHECK(doc.text() == "abcXXYZYZdef");
    doc.undo();
    CHECK(doc.text() == "abcXYZdef");
  }
  {
    Document doc(std::string("a") + "\xc3\xa9" + "bc");
    const auto r = drop_text(doc, {1, 3}, 5, true);
    REQUIRE(r);
    doc.begin();
    for (const Edit& e : r->edits) doc.apply(e);
    doc.commit();
    CHECK(doc.text() == std::string("abc") + "\xc3\xa9");
    CHECK(r->selection == Selection{3, 5});
    doc.undo();
    CHECK(doc.text() == std::string("a") + "\xc3\xa9" + "bc");
  }
  {
    Document doc("one\r\ntwo\r\nthree");
    const auto r = drop_text(doc, {0, 5}, 15, true);
    REQUIRE(r);
    doc.begin();
    for (const Edit& e : r->edits) doc.apply(e);
    doc.commit();
    CHECK(doc.text() == "two\r\nthreeone\r\n");
    CHECK(r->selection == Selection{10, 15});
    doc.undo();
    CHECK(doc.text() == "one\r\ntwo\r\nthree");
  }
}

TEST_CASE("commands: property check of drop_text over random documents", "[random]") {
  std::mt19937 rng(424242);
  int applied = 0;
  for (int round = 0; round < 3000; ++round) {
    const std::string text = random_document(rng);
    if (text.empty()) continue;
    const auto pick = [&] {
      std::size_t p = std::uniform_int_distribution<std::size_t>(0, text.size())(rng);
      while (!boundary(text, p)) --p;
      return p;
    };
    std::size_t a = pick();
    std::size_t b = pick();
    if (a > b) std::swap(a, b);
    const std::size_t target = pick();
    const bool move = std::uniform_int_distribution<int>(0, 1)(rng) == 0;
    Document doc(text);
    INFO("text: " << escaped(text) << " source: " << a << "," << b << " target: " << target << " move: " << move);
    const auto r = drop_text(doc, {a, b}, target, move);
    if (!r) continue;
    doc.begin();
    for (const Edit& e : r->edits) REQUIRE_NOTHROW(doc.apply(e));
    doc.commit();
    ++applied;
    REQUIRE(r->selection.cursor <= doc.size());
    REQUIRE(r->selection.anchor <= doc.size());
    REQUIRE_FALSE(doc.tree().validate(doc.size()).has_value());
    doc.undo();
    REQUIRE(doc.text() == text);
  }
  CHECK(applied > 1000);
}

TEST_CASE("commands: format commands on an empty line") {
  CHECK(run(code, "|\nb") == "`|`\nb");
  CHECK(run(italic, "a\n\n|\n---\n") == "a\n\n*|*\n---\n");
  CHECK(run(strike, "|") == "~~|~~");
  CHECK(run(strike, "> |") == "> ~~|~~");
  CHECK(run(strike, "~~|~~") == "|");
  CHECK(run(strike, "a\n\n~~|~~\n\nb") == "a\n\n|\n\nb");
  CHECK(run(bold, "- a\n\n  |") == "- a\n\n  **|**");
  CHECK(run(code, "- a\n- |") == "- a\n- `|`");
  CHECK(run(code, "```\n|\n```") == "none");
  CHECK(run(bullet, "|") == "- |");
  CHECK(run(bullet, "a\n|") == "a\n\n- |");
  CHECK(run(bullet, "- a\n\n  |") == "- a\n\n  - |");
  CHECK(run(ordered, "a\n\n|\n\nb") == "a\n\n1. |\n\nb");
  CHECK(run(ordered, "> |") == "> 1. |");
  CHECK(run(task, "|") == "- [ ] |");
  CHECK(run(task, "- a\n\n  |") == "- a\n\n  - [ ] |");
  CHECK(run(quote, "|") == "> |");
  CHECK(run(quote, "a\n\n|\n\nb") == "a\n\n> |\n\nb");
  CHECK(run(quote, "- a\n\n  |") == "- a\n\n  > |");
  CHECK(run(code_blk, "- a\n- |") == "- a\n- ```\n  |\n  ```");
  CHECK(run(code_blk, "- a\n\n  |") == "- a\n\n  ```\n  |\n  ```");
  CHECK(run(code_blk, "> |") == "> ```\n> |\n> ```");
  CHECK(run(bullet, "^a\n\nb|") == "- ^a\n\n- b|");
}

TEST_CASE("commands: insert a link on an empty line or a line of spaces") {
  const auto ln = link_cmd("t", "u");
  CHECK(run(ln, "|") == "[t](u)|");
  CHECK(run(ln, "Above.\n|\nBelow.") == "Above.\n[t](u)|\nBelow.");
  CHECK(run(ln, "Above.\n\n|\n\nBelow.") == "Above.\n\n[t](u)|\n\nBelow.");
  CHECK(run(ln, "Above.\n  |\nBelow.") == "Above.\n  [t](u)|\nBelow.");
  CHECK(run(ln, "  |") == "  [t](u)|");
  CHECK(run(ln, "- Item\n|\n  More.") == "- Item\n[t](u)|\n  More.");
  CHECK(run(ln, "- Item\n\n  |") == "- Item\n\n  [t](u)|");
  CHECK(run(ln, "> a\n> |") == "> a\n> [t](u)|");
  CHECK(run(link_cmd("", "u"), "Above.\n|\nBelow.") == "Above.\n[|](u)\nBelow.");
}

TEST_CASE("commands: format commands on the empty lines of the hand tests") {
  const std::string between = "Above.\n|\nBelow.";
  CHECK(run(code, between) == "Above.\n`|`\nBelow.");
  CHECK(run(strike, between) == "Above.\n~~|~~\nBelow.");
  CHECK(run(bullet, between) == "Above.\n\n- |\n\nBelow.");
  CHECK(run(ordered, between) == "Above.\n\n1. |\n\nBelow.");
  CHECK(run(task, between) == "Above.\n- [ ] |\n\nBelow.");
  CHECK(run(quote, between) == "Above.\n> |\n\nBelow.");
  CHECK(run(code_blk, between) == "Above.\n```\n|\n```\nBelow.");
  CHECK(run(hr, between) == "Above.\n\n---\n|Below.");

  const std::string item = "- Item\n|\n  More.";
  CHECK(run(code, item) == "- Item\n`|`\n  More.");
  CHECK(run(bold, item) == "- Item\n**|**\n  More.");
  CHECK(run(bullet, item) == "- Item\n\n  - |\n\n  More.");
  CHECK(run(ordered, item) == "- Item\n\n  1. |\n\n  More.");
  CHECK(run(task, item) == "- Item\n  - [ ] |\n\n  More.");
  CHECK(run(quote, item) == "- Item\n  > |\n\n  More.");
  CHECK(run(code_blk, item) == "- Item\n  ```\n  |\n  ```\n  More.");
  CHECK(run(hr, item) == "- Item\n\n  ---\n  |More.");

  CHECK(run(code, "Above.\n\n|\nBelow.") == "Above.\n\n`|`\nBelow.");
  CHECK(run(h1, item) == "- Item\n  # |\n  More.");
  CHECK(run(quote, "- a\n- |") == "- a\n> - |");
  CHECK(run(code_blk, "> a\n|>\n> b") == "> a\n> ```\n>|\n> ```\n> b");
}

TEST_CASE("commands: every command on every kind of empty line") {
  using AnyCommand = std::function<std::optional<CommandResult>(const Document&, Selection)>;
  const std::initializer_list<std::pair<const char*, AnyCommand>> cmds = {
      {"bold", bold}, {"italic", italic}, {"strike", strike}, {"code", code}, {"link", link_cmd("t", "u")},
      {"h1", h1}, {"h3", h3}, {"h0", h0}, {"bullet", bullet}, {"ordered", ordered}, {"task", task}, {"quote", quote},
      {"code_blk", code_blk}, {"hr", hr},
      {"table", [](const Document& d, Selection s) { return insert_table(d, s, 2, 2, "\n"); }},
      {"renumber", [](const Document& d, Selection s) { return renumber(d, s); }},
      {"enter", enter_lf}, {"shift_enter", break_lf}, {"tab", indent_list}, {"shift_tab", outdent_list},
      {"backspace", delete_markup_backward}};
  struct Spot {
    const char* name;
    const char* text;  
    const char* nothing;
  };
  const std::initializer_list<Spot> spots = {
      {"empty doc", "@", " renumber enter shift_enter tab shift_tab backspace"},
      {"between paragraphs", "Above.\n@\nBelow.", " renumber enter shift_enter tab shift_tab backspace"},
      {"spaces between", "Above.\n  @\nBelow.", " renumber enter shift_enter tab shift_tab backspace"},
      {"double blank", "Above.\n\n@\n\nBelow.", " renumber enter shift_enter tab shift_tab backspace"},
      {"after paragraph, end", "Above.\n@", " renumber enter shift_enter tab shift_tab backspace"},
      {"item empty line", "- Item\n@\n  More.", " renumber enter shift_enter backspace"},
      {"item indented line", "- Item\n  @\n  More.", " renumber"},
      {"item spaces line", "- Item\n\n  @", " renumber enter shift_enter tab shift_tab backspace"},
      {"empty item", "- a\n- @", " hr renumber"},
      {"empty quote line", "> a\n> @", " renumber tab shift_tab"},
      {"before heading", "@\n# H", " renumber enter shift_enter tab shift_tab backspace"},
      {"before list", "@\n- x", " renumber enter shift_enter tab shift_tab backspace"},
      {"before table", "@\n| a | b |\n| - | - |", " renumber enter shift_enter tab shift_tab backspace"},
      {"after table", "| a | b |\n| - | - |\n\n@", " renumber enter shift_enter tab shift_tab backspace"},
      {"between tables", "| a |\n| - |\n\n@\n\n| b |\n| - |", " renumber enter shift_enter tab shift_tab backspace"}};
  for (const auto& [spot, text, nothing] : spots) {
    std::string none;
    std::string t = text;
    const std::size_t at = t.find('@');
    t.erase(at, 1);
    for (const auto& [name, cmd] : cmds) {
      Document doc(t);
      const auto r = cmd(doc, {at, at});
      if (!r) {
        none += std::string(" ") + name;
        continue;
      }
      INFO(spot << " " << name);
      doc.begin();
      for (const Edit& e : r->edits) REQUIRE_NOTHROW(doc.apply(e));
      doc.commit();
      REQUIRE_FALSE(doc.tree().validate(doc.size()).has_value());
    }
    INFO(spot);
    CHECK(none == nothing);
  }
}
