#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <set>
#include <tuple>
#include <string>
#include <string_view>
#include <vector>

#include "lumos/document.hpp"
#include "lumos/log.hpp"
#include "lumos/parser.hpp"
#include "lumos/syntax.hpp"

using lumos::Alignment;
using lumos::Dialect;
using lumos::Edit;
using lumos::Node;
using lumos::NodeId;
using lumos::NodeKind;
using lumos::SyntaxTree;

namespace {

void check_invariants(const SyntaxTree& tree, std::string_view src) {
  const auto problem = tree.validate(src.size());
  INFO("source: " << src);
  INFO("tree: " << tree.to_debug_string());
  CHECK(problem.value_or("") == "");
}

struct Doc {
  std::string src;
  std::unique_ptr<lumos::Parser> parser;

  explicit Doc(std::string text, const Dialect& dialect = {})
      : src(std::move(text)), parser(lumos::make_parser(dialect)) {
    parser->parse(src);
    check_invariants(tree(), src);
  }

  const SyntaxTree& tree() const { return parser->tree(); }
  const Node& node(NodeId id) const { return tree().node(id); }
  std::string_view text(NodeId id) const { return lumos::node_text(src, node(id)); }

  std::vector<NodeId> all(NodeKind kind) const {
    std::vector<NodeId> out;
    for (NodeId i = 0; i < tree().size(); ++i)
      if (tree().node(i).kind == kind) out.push_back(i);
    return out;
  }
  std::vector<std::string_view> texts(NodeKind kind) const {
    std::vector<std::string_view> out;
    for (NodeId id : all(kind)) out.push_back(text(id));
    return out;
  }
  NodeId first(NodeKind kind) const {
    const auto ids = all(kind);
    INFO("kind: " << lumos::kind_name(kind) << " tree: " << tree().to_debug_string());
    REQUIRE(!ids.empty());
    return ids.front();
  }
  std::vector<NodeKind> kids(NodeId id) const {
    std::vector<NodeKind> out;
    for (NodeId c : tree().children(id)) out.push_back(node(c).kind);
    return out;
  }
  std::vector<std::string_view> kid_texts(NodeId id) const {
    std::vector<std::string_view> out;
    for (NodeId c : tree().children(id)) out.push_back(text(c));
    return out;
  }
};

using SV = std::vector<std::string_view>;
using KV = std::vector<NodeKind>;

std::string read_sample() {
  const auto path = std::filesystem::path(LUMOS_TEST_DATA_DIR) / "sample.md";
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.good());
  const auto size = std::filesystem::file_size(path);
  std::string out(static_cast<std::size_t>(size), '\0');
  in.read(out.data(), static_cast<std::streamsize>(size));
  REQUIRE(in.good());
  return out;
}

const std::vector<std::string>& corpus() {
  static const std::vector<std::string> inputs = {
      "",
      "\n",
      "a",
      "# Title\n",
      "# Title ##\n## Sub *x* ##   \n###### six\n#\n# #\n#5 not\n",
      "Title\n=====\n\nSub\n---\n",
      "Multi\nline *setext*\n===\n",
      "**a** __b__ *c* _d_ ***e*** `f` ~~g~~ ~h~\n",
      "**a *b** c*\n**open\n*x\n",
      "[a](b \"t\") [r][1] [c][] [d] ![i](p.png 't') <http://x.y> <a@b.c>\n\n[1]: /u\n[c]: /v\n",
      "[undefined] and [x][nope] and [](empty)\n",
      "- a\n* b\n+ c\n\n1. x\n2. y\n\n1) p\n2) q\n",
      "- a\n  - b\n    - c\n      text\n- d\n\n  para\n",
      "- [ ] open\n- [x] done\n- [X] done too\n- [ ]\n",
      "> q\n> line\n>> deep\n>\n> > deeper\n> *em\n> across*\n",
      "```cpp x\ncode\n```\n\n~~~\nt\n~~~\n\n```\nunclosed\n",
      "    indented\n    code\n\n  \n    more\n",
      "---\n\n***\n\n___\n",
      "| A | :b | c: |\n|:--|:-:|--:|\n| *1* | 2 |\n| x \\| y |\n",
      "> | a |\n> |---|\n> | b |\n",
      "<div>\n*not em*\n</div>\n\na <b>x</b> <!-- c -->\n",
      "line  \nnext\\\nend\n",
      "\\* \\_ \\` \\# \\[ \\\\ &amp; &#35;\n",
      "# H\r\n\r\nSome *em* text\r\nline2  \r\nx\r\n- a\r\n- b\r\n",
      "Ünïcödé äöü € 日本語 *émphasis* 🎉 **ß**\n",
      "---\ntitle: x\n---\n\n# After front matter\n",
      "costs $5 and $6\n",
      "- > q\n  > r\n- ```\n  code\n  ```\n",
      "1. a\n\n   b\n\n2. c\n",
      "> ```\n> q\n> ```\n",
      "[link\nacross lines](u)\n",
      "*a\n\nb*\n",
      "``a ` b`` `` ` ``\n",
      "<http://x.y/path?q=1>\n",
      "\t- tab\n\t\tindent\n",
      "* * *\n- - -\n",
      "[foo]: /url \"title\"\n\n[foo]\n",
      "[FOO]: /url\n\n[Foo] [foo][] [x][FOO]\n",
      "![a][img]\n\n[img]: /i.png\n",
      "a\\\n",
      "|a|\n|-|\n",
      "\n\n\n# x\n\n\n",
      "> \n",
      "-\n",
      "1.\n",
      "<",
      "[",
      "`",
      "*",
      "# ",
  };
  return inputs;
}

}  

TEST_CASE("parser: ATX headings") {
  Doc d("# Title\n## Sub *x* ##\n###### six\n");
  const auto h = d.all(NodeKind::heading);
  REQUIRE(h.size() == 3);
  CHECK(d.node(h[0]).level == 1);
  CHECK(d.node(h[1]).level == 2);
  CHECK(d.node(h[2]).level == 6);
  CHECK_FALSE(d.node(h[0]).setext);
  CHECK(d.text(h[0]) == "# Title\n");
  CHECK(d.kids(h[0]) == KV{NodeKind::heading_marker, NodeKind::text});
  CHECK(d.kid_texts(h[0]) == SV{"#", "Title"});
  CHECK(d.kids(h[1]) == KV{NodeKind::heading_marker, NodeKind::text, NodeKind::emphasis, NodeKind::heading_marker});
  CHECK(d.kid_texts(h[1]) == SV{"##", "Sub ", "*x*", "##"});
  CHECK(d.kid_texts(h[2]) == SV{"######", "six"});

  CHECK(Doc("# Title\n").tree().to_debug_string() ==
        "(document@0..8 (heading@0..8 level=1 (heading_marker@0..1) (text@2..7)))");
}

TEST_CASE("parser: ATX closing sequence needs a space before it") {
  Doc d("# C#\n");
  const NodeId h = d.first(NodeKind::heading);
  CHECK(d.kid_texts(h) == SV{"#", "C#"});
}

TEST_CASE("parser: setext headings") {
  Doc d("Title\n=====\n\nSub *x*\n---\n");
  const auto h = d.all(NodeKind::heading);
  REQUIRE(h.size() == 2);
  CHECK(d.node(h[0]).level == 1);
  CHECK(d.node(h[0]).setext);
  CHECK(d.node(h[1]).level == 2);
  CHECK(d.node(h[1]).setext);
  CHECK(d.kids(h[0]) == KV{NodeKind::text, NodeKind::heading_marker});
  CHECK(d.kid_texts(h[0]) == SV{"Title", "====="});
  CHECK(d.kid_texts(h[1]) == SV{"Sub ", "*x*", "---"});
  CHECK(d.all(NodeKind::paragraph).empty());
}

TEST_CASE("parser: strong with ** and __") {
  Doc d("a **b** c __d__\n");
  CHECK(d.texts(NodeKind::strong) == SV{"**b**", "__d__"});
  CHECK(d.texts(NodeKind::emphasis_delimiter) == SV{"**", "**", "__", "__"});
  const NodeId s = d.first(NodeKind::strong);
  CHECK(d.kids(s) == KV{NodeKind::emphasis_delimiter, NodeKind::text, NodeKind::emphasis_delimiter});
  CHECK(d.kid_texts(s) == SV{"**", "b", "**"});
  CHECK(d.texts(NodeKind::text) == SV{"a ", "b", " c ", "d"});
}

TEST_CASE("parser: emphasis with * and _") {
  Doc d("*a* _b_\n");
  CHECK(d.texts(NodeKind::emphasis) == SV{"*a*", "_b_"});
  CHECK(d.texts(NodeKind::emphasis_delimiter) == SV{"*", "*", "_", "_"});
  CHECK(d.all(NodeKind::strong).empty());
}

TEST_CASE("parser: nested ***both***") {
  Doc d("***x***\n");
  const NodeId em = d.first(NodeKind::emphasis);
  CHECK(d.text(em) == "***x***");
  CHECK(d.kids(em) == KV{NodeKind::emphasis_delimiter, NodeKind::strong, NodeKind::emphasis_delimiter});
  CHECK(d.kid_texts(em) == SV{"*", "**x**", "*"});
  const NodeId st = d.first(NodeKind::strong);
  CHECK(d.node(st).parent == em);
  CHECK(d.kid_texts(st) == SV{"**", "x", "**"});
}

TEST_CASE("parser: code spans") {
  Doc d("a `code` and ``x ` y``\n");
  CHECK(d.texts(NodeKind::code_span) == SV{"`code`", "``x ` y``"});
  CHECK(d.texts(NodeKind::code_span_delimiter) == SV{"`", "`", "``", "``"});
  const NodeId c = d.first(NodeKind::code_span);
  CHECK(d.kid_texts(c) == SV{"`", "code", "`"});
}

TEST_CASE("parser: strikethrough") {
  Doc d("a ~~gone~~ b\n");
  const auto s = d.all(NodeKind::strikethrough);
  REQUIRE(s.size() == 1);
  CHECK(d.text(s[0]) == "~~gone~~");
  CHECK(d.kid_texts(s[0]) == SV{"~~", "gone", "~~"});

  Dialect off;
  off.strikethrough = false;
  Doc p("a ~~gone~~ b\n", off);
  CHECK(p.all(NodeKind::strikethrough).empty());
  CHECK(p.all(NodeKind::emphasis_delimiter).empty());
  CHECK(p.texts(NodeKind::text) == SV{"a ~~gone~~ b"});
}

TEST_CASE("parser: inline link with title") {
  Doc d("x [a](b \"t\") y\n");
  const NodeId l = d.first(NodeKind::link);
  CHECK(d.text(l) == "[a](b \"t\")");
  CHECK(d.kids(l) == KV{NodeKind::link_delimiter, NodeKind::link_text, NodeKind::link_delimiter,
                        NodeKind::link_delimiter, NodeKind::link_destination, NodeKind::link_title,
                        NodeKind::link_delimiter});
  CHECK(d.kid_texts(l) == SV{"[", "a", "]", "(", "b", "\"t\"", ")"});
  CHECK(d.node(l).start == 2);
  CHECK(d.node(l).end == 12);
}

TEST_CASE("parser: reference links need a definition") {
  Doc d("[a][1] [B][] [c] [x] [y][nope]\n\n[1]: /u \"T\"\n[b]: /v\n[C]: /w\n");
  CHECK(d.texts(NodeKind::link) == SV{"[a][1]", "[B][]", "[c]"});
  const NodeId l = d.first(NodeKind::link);
  CHECK(d.kids(l) == KV{NodeKind::link_delimiter, NodeKind::link_text, NodeKind::link_delimiter, NodeKind::link_label});
  CHECK(d.kid_texts(l) == SV{"[", "a", "]", "[1]"});
  const NodeId para = d.first(NodeKind::paragraph);
  CHECK(d.kid_texts(para).back() == " [x] [y][nope]");

  const auto defs = d.all(NodeKind::link_reference_definition);
  REQUIRE(defs.size() == 3);
  CHECK(d.text(defs[0]) == "[1]: /u \"T\"\n");
  CHECK(d.kids(defs[0]) == KV{NodeKind::link_label, NodeKind::link_delimiter, NodeKind::link_destination,
                              NodeKind::link_title});
  CHECK(d.kid_texts(defs[0]) == SV{"[1]", ":", "/u", "\"T\""});
}

TEST_CASE("parser: images") {
  Doc d("![alt *x*](src.png 't') ![r][img]\n\n[img]: /i.png\n");
  CHECK(d.texts(NodeKind::image) == SV{"![alt *x*](src.png 't')", "![r][img]"});
  const NodeId img = d.first(NodeKind::image);
  CHECK(d.kid_texts(img) == SV{"![", "alt *x*", "]", "(", "src.png", "'t'", ")"});
  CHECK(d.texts(NodeKind::link_text) == SV{"alt *x*", "r"});
  CHECK(d.texts(NodeKind::emphasis) == SV{"*x*"});
}

TEST_CASE("parser: autolinks") {
  Doc d("see <http://x.y/a?b=1> or <me@x.y>\n");
  CHECK(d.texts(NodeKind::autolink) == SV{"<http://x.y/a?b=1>", "<me@x.y>"});
  const NodeId a = d.first(NodeKind::autolink);
  CHECK(d.kids(a) == KV{NodeKind::link_delimiter, NodeKind::text, NodeKind::link_delimiter});
  CHECK(d.kid_texts(a) == SV{"<", "http://x.y/a?b=1", ">"});
}

TEST_CASE("parser: bullet lists with -, * and +") {
  Doc d("- a\n- b\n\n* c\n\n+ d\n");
  const auto lists = d.all(NodeKind::list);
  REQUIRE(lists.size() == 3);
  for (NodeId l : lists) CHECK_FALSE(d.node(l).ordered);
  CHECK(d.texts(NodeKind::list_marker) == SV{"-", "-", "*", "+"});
  CHECK(d.texts(NodeKind::list_item) == SV{"- a\n", "- b\n", "* c\n", "+ d\n"});
  const NodeId item = d.first(NodeKind::list_item);
  CHECK(d.kids(item) == KV{NodeKind::list_marker, NodeKind::paragraph});
  CHECK(d.kid_texts(item) == SV{"-", "a\n"});
}

TEST_CASE("parser: ordered lists with 1. and 1)") {
  Doc d("1. a\n2. b\n\n1) c\n");
  const auto lists = d.all(NodeKind::list);
  REQUIRE(lists.size() == 2);
  CHECK(d.node(lists[0]).ordered);
  CHECK(d.node(lists[1]).ordered);
  CHECK(d.texts(NodeKind::list_marker) == SV{"1.", "2.", "1)"});
  CHECK(d.texts(NodeKind::paragraph) == SV{"a\n", "b\n", "c\n"});
}

TEST_CASE("parser: nested lists") {
  Doc d("- a\n  - b\n    1. c\n- d\n");
  const auto lists = d.all(NodeKind::list);
  REQUIRE(lists.size() == 3);
  CHECK(d.text(lists[0]) == "- a\n  - b\n    1. c\n- d\n");
  CHECK(d.text(lists[1]) == "- b\n    1. c\n");
  CHECK(d.text(lists[2]) == "1. c\n");
  CHECK(d.node(lists[2]).ordered);
  CHECK(d.tree().depth(lists[1]) == 3);  
  CHECK(d.node(d.node(lists[1]).parent).kind == NodeKind::list_item);
  CHECK(d.texts(NodeKind::block_continuation) == SV{"  ", "    "});
}

TEST_CASE("parser: task lists") {
  Doc d("- [ ] open\n- [x] done\n");
  const auto t = d.all(NodeKind::task_marker);
  REQUIRE(t.size() == 2);
  CHECK(d.text(t[0]) == "[ ]");
  CHECK_FALSE(d.node(t[0]).checked);
  CHECK(d.text(t[1]) == "[x]");
  CHECK(d.node(t[1]).checked);
  CHECK(d.texts(NodeKind::paragraph) == SV{"open\n", "done\n"});

  Dialect off;
  off.task_lists = false;
  Doc p("- [ ] open\n", off);
  CHECK(p.all(NodeKind::task_marker).empty());
  CHECK(p.texts(NodeKind::paragraph) == SV{"[ ] open\n"});
  CHECK(p.texts(NodeKind::text) == SV{"[ ] open"});
}

TEST_CASE("parser: block quotes") {
  Doc d("> a *b\n> c* d\n>> deep\n");
  const auto q = d.all(NodeKind::block_quote);
  REQUIRE(q.size() == 2);
  CHECK(d.text(q[0]) == "> a *b\n> c* d\n>> deep\n");
  CHECK(d.text(q[1]) == "> deep\n");
  CHECK(d.node(q[1]).parent == q[0]);
  CHECK(d.texts(NodeKind::block_quote_marker) == SV{">", ">"});
  CHECK(d.texts(NodeKind::paragraph) == SV{"a *b\n> c* d\n", "deep\n"});
  const NodeId em = d.first(NodeKind::emphasis);
  CHECK(d.kids(em) == KV{NodeKind::emphasis_delimiter, NodeKind::text, NodeKind::soft_break,
                         NodeKind::block_continuation, NodeKind::text, NodeKind::emphasis_delimiter});
  CHECK(d.kid_texts(em) == SV{"*", "b", "\n", "> ", "c", "*"});
  CHECK(d.kid_texts(q[0]) == SV{">", "a *b\n> c* d\n", ">", "> deep\n"});
}

TEST_CASE("parser: fenced code blocks") {
  Doc d("```cpp title\nint a;\n```\n\n~~~\nt\n~~~\n");
  const auto c = d.all(NodeKind::code_block);
  REQUIRE(c.size() == 2);
  CHECK(d.node(c[0]).fenced);
  CHECK(d.node(c[1]).fenced);
  CHECK(d.kids(c[0]) == KV{NodeKind::code_fence_delimiter, NodeKind::info_string, NodeKind::code_content,
                           NodeKind::code_fence_delimiter});
  CHECK(d.kid_texts(c[0]) == SV{"```", "cpp title", "int a;\n", "```"});
  CHECK(d.kid_texts(c[1]) == SV{"~~~", "t\n", "~~~"});
  CHECK(d.text(c[0]) == "```cpp title\nint a;\n```\n");
}

TEST_CASE("parser: fenced code in a block quote") {
  Doc d("> ```\n> q\n> ```\n");
  const NodeId c = d.first(NodeKind::code_block);
  CHECK(d.kid_texts(c) == SV{"```", "> q\n", "> ", "```"});
  CHECK(d.kid_texts(d.first(NodeKind::code_content)) == SV{"> "});
}

TEST_CASE("parser: indented code") {
  Doc d("    a\n    b\n");
  const NodeId c = d.first(NodeKind::code_block);
  CHECK_FALSE(d.node(c).fenced);
  CHECK(d.text(c) == "a\n    b\n");
  CHECK(d.kids(c) == KV{NodeKind::code_content});
  CHECK(d.kid_texts(d.first(NodeKind::code_content)) == SV{"    "});
}

TEST_CASE("parser: thematic breaks") {
  Doc d("a\n\n---\n\n***\n\n___\n");
  CHECK(d.texts(NodeKind::thematic_break) == SV{"---\n", "***\n", "___\n"});
}

TEST_CASE("parser: GFM table with alignment") {
  Doc d("| A | :b | c: |\n|:--|:-:|--:|\n| *1* | 2 | x \\| y |\n");
  const NodeId t = d.first(NodeKind::table);
  CHECK(d.kids(t) == KV{NodeKind::table_header_row, NodeKind::table_delimiter_row, NodeKind::table_row});
  CHECK(d.texts(NodeKind::table_cell) == SV{"A", ":b", "c:", "*1*", "2", "x \\| y"});
  CHECK(d.texts(NodeKind::table_delimiter_cell) == SV{":--", ":-:", "--:"});
  CHECK(d.texts(NodeKind::table_pipe).size() == 12);
  std::vector<Alignment> aligns;
  for (NodeId c : d.all(NodeKind::table_cell)) aligns.push_back(d.node(c).align);
  CHECK(aligns == std::vector<Alignment>{Alignment::left, Alignment::center, Alignment::right, Alignment::left,
                                         Alignment::center, Alignment::right});
  const NodeId header = d.first(NodeKind::table_header_row);
  CHECK(d.kid_texts(header) == SV{"|", "A", "|", ":b", "|", "c:", "|"});
  CHECK(d.texts(NodeKind::emphasis) == SV{"*1*"});
  CHECK(d.texts(NodeKind::escape) == SV{"\\"});
}

TEST_CASE("parser: HTML block and inline HTML") {
  Doc d("<div>\n*x*\n</div>\n\na <b>y</b> <!-- c -->\n");
  CHECK(d.texts(NodeKind::html_block) == SV{"<div>\n*x*\n</div>\n"});
  CHECK(d.all(NodeKind::emphasis).empty());
  CHECK(d.texts(NodeKind::html_inline) == SV{"<b>", "</b>", "<!-- c -->"});
}

TEST_CASE("parser: hard breaks") {
  Doc d("a  \nb\\\nc\n");
  CHECK(d.texts(NodeKind::hard_break) == SV{"  \n", "\\\n"});
  CHECK(d.texts(NodeKind::text) == SV{"a", "b", "c"});
}

TEST_CASE("parser: backslash escapes and entities") {
  Doc d("\\*a\\* \\_ \\\\ &amp; &#35;\n");
  CHECK(d.texts(NodeKind::escape) == SV{"\\*", "\\*", "\\_", "\\\\"});
  CHECK(d.texts(NodeKind::entity) == SV{"&amp;", "&#35;"});
  CHECK(d.all(NodeKind::emphasis).empty());
}

TEST_CASE("parser: CRLF line endings") {
  Doc d("# H\r\n\r\nSome *em* text\r\nline2  \r\nx\r\n- a\r\n");
  const NodeId h = d.first(NodeKind::heading);
  CHECK(d.text(h) == "# H\r\n");
  CHECK(d.kid_texts(h) == SV{"#", "H"});
  CHECK(d.texts(NodeKind::paragraph) == SV{"Some *em* text\r\nline2  \r\nx\r\n", "a\r\n"});
  CHECK(d.texts(NodeKind::emphasis) == SV{"*em*"});
  CHECK(d.texts(NodeKind::hard_break) == SV{"  \r\n"});
  CHECK(d.texts(NodeKind::list_marker) == SV{"-"});
}

TEST_CASE("parser: UTF-8 multi-byte text") {
  const std::string src = "Ünï € 日本 *émph* 🎉 **ß**\n";
  Doc d(src);
  CHECK(d.texts(NodeKind::emphasis) == SV{"*émph*"});
  CHECK(d.texts(NodeKind::strong) == SV{"**ß**"});
  const NodeId em = d.first(NodeKind::emphasis);
  CHECK(d.node(em).start == src.find("*émph*"));
  CHECK(d.kid_texts(em) == SV{"*", "émph", "*"});
  CHECK(d.texts(NodeKind::text).front() == "Ünï € 日本 ");
}

TEST_CASE("parser: front matter") {
  Doc d("---\ntitle: x\n---\n\n# H\n");
  CHECK(d.texts(NodeKind::front_matter) == SV{"---\ntitle: x\n---\n"});
  CHECK(d.all(NodeKind::thematic_break).empty());
}

TEST_CASE("parser: math is not implemented") {
  Doc d("costs $5 and $6\n");
  CHECK(d.all(NodeKind::math).empty());
  CHECK(d.texts(NodeKind::text) == SV{"costs $5 and $6"});

  Dialect on;
  on.math = true;
  Doc m("x $a+b$ y\n", on);
  CHECK(m.all(NodeKind::math).empty());
}

TEST_CASE("syntax tree: node_at, children and debug string") {
  Doc d("# a *b*\n\ntext\n");
  const SyntaxTree& t = d.tree();
  CHECK(t.node(t.node_at(0)).kind == NodeKind::heading_marker);
  CHECK(t.node(t.node_at(2)).kind == NodeKind::text);
  CHECK(t.node(t.node_at(4)).kind == NodeKind::emphasis_delimiter);
  CHECK(t.node(t.node_at(5)).kind == NodeKind::text);
  CHECK(t.node(t.node(t.node_at(5)).parent).kind == NodeKind::emphasis);
  CHECK(t.node(t.node_at(8)).kind == NodeKind::document);  
  CHECK(t.node(t.node_at(9)).kind == NodeKind::text);
  CHECK(t.node_at(1000) == t.root());
  CHECK(d.kids(t.root()) == KV{NodeKind::heading, NodeKind::paragraph});
  CHECK(t.to_debug_string() ==
        "(document@0..14 (heading@0..8 level=1 (heading_marker@0..1) (text@2..4) "
        "(emphasis@4..7 (emphasis_delimiter@4..5) (text@5..6) (emphasis_delimiter@6..7))) "
        "(paragraph@9..14 (text@9..13)))");

  for (NodeId i = 1; i < t.size(); ++i) CHECK(t.node(i).parent < i);
}

TEST_CASE("syntax tree: validate finds bad trees") {
  std::vector<Node> nodes(3);
  nodes[0].kind = NodeKind::document;
  nodes[0].end = 10;
  nodes[1].kind = NodeKind::paragraph;
  nodes[1].parent = 0;
  nodes[1].start = 0;
  nodes[1].end = 6;
  nodes[2].kind = NodeKind::paragraph;
  nodes[2].parent = 0;
  nodes[2].start = 5;
  nodes[2].end = 10;
  CHECK(SyntaxTree(nodes).validate(10).has_value());  
  nodes[2].start = 6;
  CHECK_FALSE(SyntaxTree(nodes).validate(10).has_value());
  nodes[2].end = 11;
  CHECK(SyntaxTree(nodes).validate(10).has_value());  
  CHECK(SyntaxTree().validate(0) == std::nullopt);
}

TEST_CASE("parser: invariants hold for all test inputs") {
  auto parser = lumos::make_parser();
  std::vector<std::string> inputs = corpus();
  inputs.push_back(read_sample());
  for (const std::string& src : inputs) {
    parser->parse(src);
    check_invariants(parser->tree(), src);
    for (const Node& n : parser->tree().nodes()) {
      if (n.kind == NodeKind::text) CHECK(n.size() > 0);
    }
  }
}

TEST_CASE("parser: the sample document parses into the expected kinds") {
  const std::string src = read_sample();
  Doc d(src);
  for (NodeKind k : {NodeKind::front_matter, NodeKind::heading, NodeKind::paragraph, NodeKind::strong,
                     NodeKind::emphasis, NodeKind::code_span, NodeKind::strikethrough, NodeKind::link,
                     NodeKind::image, NodeKind::autolink, NodeKind::hard_break, NodeKind::escape,
                     NodeKind::entity, NodeKind::html_inline, NodeKind::list, NodeKind::task_marker,
                     NodeKind::block_quote, NodeKind::code_block, NodeKind::thematic_break, NodeKind::table,
                     NodeKind::html_block, NodeKind::link_reference_definition}) {
    INFO(lumos::kind_name(k));
    CHECK_FALSE(d.all(k).empty());
  }
}

namespace {

void check_edit(lumos::Parser& inc, std::string& text, const Edit& e) {
  text.replace(e.offset, e.removed, e.inserted);
  inc.apply_edit(e, text);
  auto fresh = lumos::make_parser();
  fresh->parse(text);
  INFO("text after edit: " << text);
  REQUIRE(inc.tree().to_debug_string() == fresh->tree().to_debug_string());
  check_invariants(inc.tree(), text);
}

}  

TEST_CASE("parser: apply_edit gives the same tree as a fresh parse") {
  std::string text = read_sample();
  auto inc = lumos::make_parser();
  inc->parse(text);

  const std::size_t intro = text.find("Intro");
  check_edit(*inc, text, {intro, 0, "## "});
  check_edit(*inc, text, {intro, 3, ""});
  const std::size_t strong = text.find("**strong**");
  check_edit(*inc, text, {strong, 1, ""});
  check_edit(*inc, text, {strong, 0, "*"});
  const std::size_t fence = text.find("```\n\n~~~");
  check_edit(*inc, text, {fence, 4, ""});
  const std::size_t row = text.find("| d \\| e");
  check_edit(*inc, text, {row, 0, "| x | y | z |\n"});
  const std::size_t blank = text.find("lines.\n>\n");
  check_edit(*inc, text, {blank + 7, 2, ""});
  check_edit(*inc, text, {blank + 7, 0, ">\n"});
  const std::string all = text;
  check_edit(*inc, text, {0, text.size(), ""});
  check_edit(*inc, text, {0, 0, all});
}

namespace {

bool is_continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0U) == 0x80U; }

std::string larger_doc() {
  const std::string sample = read_sample();
  std::string out;
  for (int i = 0; i < 2; ++i) {
    out += sample;
    out += "\nPara " + std::to_string(i) + " with [ref][1] and [x] and [y][]\nnext line\n===\n\n";
    out += "- a\n  > quote in list\n  > more\n- b\n\n  1. inner\n  2. inner\n\n";
    out += "| h1 | h2 |\n|----|---:|\n| a | b |\n| c | d |\n\n";
    out += "````md\n```\nnot closed here\n````\n\n";
    out += "Term\n---\n\n[x]: /x-url\n\n> quote\nlazy line\n\n    code\n";
  }
  return out;
}

std::string random_block_doc(unsigned seed) {
  static const std::vector<std::string> lines = {
      "# H1\n", "## H2\n", "### H3\n", "#### H4\n", "para text *em*\n", "\n", "\n", "- item\n", "  - sub\n",
      "1. one\n", "> quote\n", "lazy\n", "```\n", "~~~\n", "    code\n", "| a | b |\n", "|---|---|\n", "[r]: /u\n",
      "see [r] and [q]\n", "Title\n", "===\n", "---\n", "<div>\n", "## \n", "#\n", "\t- tab\n", "***\n", "+++\n"};
  std::mt19937 rng(seed);
  std::string out;
  for (int i = 0; i < 150; ++i) out += lines[std::uniform_int_distribution<std::size_t>(0, lines.size() - 1)(rng)];
  return out;
}

Edit structure_edit(std::mt19937& rng, const std::string& text, std::size_t max_size = SIZE_MAX) {
  static const std::vector<std::string> pieces = {
      "a", " ", "\n", "\n\n", "*", "**", "_", "`", "```\n", "~~~\n", "````\n", "# ", "## ", "- ", "* ", "1. ",
      "2) ", "> ", ">> ", "[ ] ", "[x]: /url\n", "[x]", "[1]: /one\n", "[1]", "[y]: <y> 'T'\n", "|", "| a | b |\n",
      "|---|---|\n", "===\n", "---\n", "+++\n", "***\n", "\r\n", "    ", "\t", "~~", "\\", "<div>\n", "</div>\n",
      "<b>", "&amp;", "ä", "日本", "🎉", "[t](u)", "![i](p)", "<http://x.y>", "\\\n", "  \n"};
  auto pick = [&](std::size_t n) { return std::uniform_int_distribution<std::size_t>(0, n - 1)(rng); };
  std::size_t start = std::uniform_int_distribution<std::size_t>(0, text.size())(rng);
  if (pick(3) == 0) {
    while (start > 0 && text[start - 1] != '\n') --start;
  }
  while (start < text.size() && is_continuation(text[start])) ++start;
  std::size_t end = start;
  if (pick(3) == 0 || text.size() > max_size) {
    end = std::min(text.size(), start + pick(text.size() > max_size ? 40 : 14));
    while (end < text.size() && is_continuation(text[end])) ++end;
  }
  std::string inserted;
  const std::size_t count = pick(3);
  for (std::size_t i = 0; i < count; ++i) inserted += pieces[pick(pieces.size())];
  return Edit{start, end - start, inserted};
}

using NodeKey = std::tuple<NodeKind, std::size_t, std::size_t, std::size_t>;  

std::multiset<NodeKey> outside(const SyntaxTree& t, std::size_t from, std::size_t to, std::ptrdiff_t shift) {
  std::multiset<NodeKey> out;
  for (NodeId i = 1; i < t.size(); ++i) {
    const Node& n = t.node(i);
    if (n.end <= from && !(n.start == n.end && n.start == from)) {
      out.emplace(n.kind, n.start, n.end, t.depth(i));
    } else if (n.start >= to && !(n.start == n.end && n.start == to)) {
      out.emplace(n.kind, static_cast<std::size_t>(static_cast<std::ptrdiff_t>(n.start) + shift),
                  static_cast<std::size_t>(static_cast<std::ptrdiff_t>(n.end) + shift), t.depth(i));
    }
  }
  return out;
}

void random_edits(std::string text, unsigned seed, int steps, int eof_every = 0) {
  lumos::Document doc(text);
  std::vector<lumos::TextRegion> regions;
  doc.add_listener([&](const lumos::Change& c) { regions.assign(c.tree_changes.begin(), c.tree_changes.end()); });
  auto fresh = lumos::make_parser();
  std::mt19937 rng(seed);
  const std::size_t max_size = text.size() * 5 / 4;
  for (int step = 0; step < steps; ++step) {
    Edit e;
    while (e.removed == 0 && e.inserted.empty()) {  
      e = structure_edit(rng, text, max_size);
      if (eof_every > 0 && step % eof_every == 0) {
        e.offset = text.size();
        e.removed = 0;
      }
    }
    const SyntaxTree old_tree = doc.tree();
    text.replace(e.offset, e.removed, e.inserted);
    doc.apply(e);
    INFO("seed " << seed << " step " << step << " edit " << e.offset << " -" << e.removed << " +\"" << e.inserted
                 << "\"");
    INFO("text after edit: " << text);
    REQUIRE(doc.text() == text);
    fresh->parse(text);
    REQUIRE(doc.tree().to_debug_string() == fresh->tree().to_debug_string());
    const auto problem = doc.tree().validate(text.size());
    REQUIRE(problem.value_or("") == "");
    REQUIRE(!regions.empty());
    if (regions.size() == 1) {
      const lumos::TextRegion r = regions.front();
      REQUIRE(r.start <= e.offset);
      REQUIRE(r.new_end >= e.offset + e.inserted.size());
      const auto shift = static_cast<std::ptrdiff_t>(r.new_end) - static_cast<std::ptrdiff_t>(r.old_end);
      REQUIRE(outside(old_tree, r.start, r.old_end, shift) == outside(doc.tree(), r.start, r.new_end, 0));
    }
  }
}

}  

TEST_CASE("parser: apply_edit with random edits over the sample", "[random]") {
  for (unsigned seed = 1; seed <= 5; ++seed) random_edits(read_sample(), seed, 1000);  
}

TEST_CASE("parser: apply_edit with random edits over a larger document", "[random]") {
  for (unsigned seed = 11; seed <= 15; ++seed) random_edits(larger_doc(), seed, 1000);  
}

TEST_CASE("parser: apply_edit with random edits over random block documents", "[random]") {
  for (unsigned seed = 31; seed <= 40; ++seed) random_edits(random_block_doc(seed), seed, 300, 5);
}

TEST_CASE("parser: apply_edit with random edits over CRLF text", "[random]") {
  std::string text = larger_doc();
  std::string crlf;
  for (char c : text) {
    if (c == '\n') crlf += '\r';
    crlf += c;
  }
  random_edits(crlf, 21, 1000);
}

TEST_CASE("parser: apply_edit cases found by random edits") {
  struct Case {
    std::string before;
    Edit edit;
  };
  const std::vector<Case> cases = {
      {"---\ne\n- /\n", {10, 0, "---\n*"}},
      {"--ample\r\n---\n`\n#\n---\r", {2, 9, ""}},
      {"---\n---\n>\nv\n\n---\n", {4, 0, "<b>"}},
      {" \n=\n***\n---", {1, 0, "---\n2) "}},
      {"---\n\n---\n\t[y]:'\nr", {17, 0, "<div>\n*"}},
      {"#\n```\n```\n>\ne\n", {12, 2, "## "}},
      {"><v>\n>\n>N\n", {10, 0, "```\n    "}},
      {"```\n```\n[y]:> ''\n<b>\n    ```*\n~~~\n\nl\n- *\n~", {28, 0, "---\n[y]: <y> 'T'\n"}},
      {"```\n```\n[y]:> ''\n<b>\n]\n    ```v\n>\n*\n~~~\n\n[\n\n*\n>\n-", {30, 0, "---\n[y]: <y> 'T'\n"}},
      {"~~~\n~~~\nt\n\tn||\n-||", {16, 0, "_"}},
      {"1.\n1.\n1. 1. ", {12, 0, "\\n"}},
      {"# T\n\npara\n", {10, 0, "- "}},
  };
  for (const Case& c : cases) {
    auto inc = lumos::make_parser();
    inc->parse(c.before);
    std::string after = c.before;
    after.replace(c.edit.offset, c.edit.removed, c.edit.inserted);
    inc->apply_edit(c.edit, after);
    auto fresh = lumos::make_parser();
    fresh->parse(after);
    INFO("before: " << c.before << "\nafter: " << after);
    CHECK(inc->tree().to_debug_string() == fresh->tree().to_debug_string());
    check_invariants(inc->tree(), after);
  }
}

TEST_CASE("parser: a list marker at the end of the text without a line ending") {
  for (const std::string src : {"- ", "# T\n\n- ", "1. ", "x\n- ", "a\n\n> "}) {
    Doc d(src);
  }
  CHECK(Doc("x\n- ").tree().to_debug_string() ==
        "(document@0..4 (heading@0..4 level=2 setext (text@0..1) (heading_marker@2..3)))");
}

TEST_CASE("parser: apply_edit that does not match falls back to a full parse") {
  auto p = lumos::make_parser();
  p->parse("abc\n");
  p->apply_edit({0, 100, "x"}, "# new\n");
  CHECK(p->tree().to_debug_string() == "(document@0..6 (heading@0..6 level=1 (heading_marker@0..1) (text@2..5)))");
  auto q = lumos::make_parser();
  q->apply_edit({0, 0, "a"}, "a");
  CHECK(q->tree().to_debug_string() == "(document@0..1 (paragraph@0..1 (text@0..1)))");
}

TEST_CASE("parser: debug log has parse events") {
  std::vector<std::string> lines;
  lumos::set_log_sink([&](lumos::LogLevel, std::string_view line) { lines.emplace_back(line); });
  lumos::set_log_level(lumos::LogLevel::debug);
  auto p = lumos::make_parser();
  p->parse("[nope]\n");
  lumos::set_log_sink(nullptr);
  lumos::set_log_level(lumos::LogLevel::info);

  auto has = [&](std::string_view needle) {
    for (const auto& l : lines)
      if (l.find(needle) != std::string::npos) return true;
    return false;
  };
  CHECK(has("comp=parser event=parse_start bytes=7"));
  CHECK(has("comp=parser event=parse_end nodes=3"));
}

TEST_CASE("parser: dialect is kept") {
  Dialect d;
  d.math = true;
  auto p = lumos::make_parser(d);
  CHECK(p->dialect() == d);
  CHECK(p->tree().size() == 1);
  CHECK(p->tree().node(0).kind == NodeKind::document);
}
