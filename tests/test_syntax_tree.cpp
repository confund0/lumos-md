
#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include "lumos/syntax.hpp"

using lumos::Node;
using lumos::NodeClass;
using lumos::NodeId;
using lumos::NodeKind;
using lumos::no_node;
using lumos::SyntaxTree;
using lumos::TreeEdit;

namespace {

Node make(NodeKind kind, std::size_t start, std::size_t end, NodeId parent) {
  Node n;
  n.kind = kind;
  n.start = start;
  n.end = end;
  n.parent = parent;
  return n;
}

SyntaxTree two_paragraphs() {
  return SyntaxTree({make(NodeKind::document, 0, 22, no_node), make(NodeKind::paragraph, 0, 10, 0),
                     make(NodeKind::text, 0, 9, 1), make(NodeKind::paragraph, 11, 22, 0),
                     make(NodeKind::text, 11, 21, 3)});
}

}  

TEST_CASE("syntax: node classes") {
  CHECK(lumos::node_class(NodeKind::text) == NodeClass::content);
  CHECK(lumos::node_class(NodeKind::code_content) == NodeClass::content);
  CHECK(lumos::node_class(NodeKind::html_block) == NodeClass::content);
  CHECK(lumos::node_class(NodeKind::soft_break) == NodeClass::marker);
  CHECK(lumos::node_class(NodeKind::thematic_break) == NodeClass::marker);
  CHECK(lumos::node_class(NodeKind::block_continuation) == NodeClass::marker);
  CHECK(lumos::node_class(NodeKind::paragraph) == NodeClass::structure);
  CHECK(lumos::node_class(NodeKind::list) == NodeClass::structure);
  CHECK(lumos::kind_name(NodeKind::soft_break) == "soft_break");
  CHECK(lumos::node_kind_count == static_cast<std::size_t>(NodeKind::math_delimiter) + 1);
}

TEST_CASE("syntax: to_debug_string is a loop") {
  SyntaxTree t = two_paragraphs();
  CHECK(t.to_debug_string() ==
        "(document@0..22 (paragraph@0..10 (text@0..9)) (paragraph@11..22 (text@11..21)))");
  const std::size_t depth = 100000;
  std::vector<Node> nodes{make(NodeKind::document, 0, depth, no_node)};
  for (std::size_t i = 0; i < depth; ++i)
    nodes.push_back(make(NodeKind::block_quote, i, depth, static_cast<NodeId>(i)));
  nodes[5].tight = true;  
  SyntaxTree deep(std::move(nodes));
  const std::string s = deep.to_debug_string();
  CHECK(s.size() > depth * 10);
  CHECK(s.substr(s.size() - 3) == ")))");
  CHECK_FALSE(deep.validate(depth).has_value());
}

TEST_CASE("syntax: debug string shows the new node fields") {
  Node list = make(NodeKind::list, 0, 4, 0);
  list.tight = true;
  list.variant = '-';
  Node code = make(NodeKind::code_content, 0, 4, 1);
  code.virtual_spaces = 2;
  SyntaxTree t({make(NodeKind::document, 0, 4, no_node), list, code});
  CHECK(t.to_debug_string() == "(document@0..4 (list@0..4 tight variant=45 (code_content@0..4 vs=2)))");
}

TEST_CASE("syntax: validate_coverage") {
  const std::string text = "aaaa bbbb\n\ncccc dddd\n";
  const std::string padded = text + " ";
  SyntaxTree ok = two_paragraphs();
  CHECK_FALSE(ok.validate_coverage(padded).has_value());

  SyntaxTree gap({make(NodeKind::document, 0, 10, no_node), make(NodeKind::paragraph, 0, 10, 0),
                  make(NodeKind::text, 0, 3, 1)});
  auto e = gap.validate_coverage(text.substr(0, 10));
  REQUIRE(e.has_value());
  CHECK(e->find("byte 3") != std::string::npos);

  SyntaxTree bad({make(NodeKind::document, 0, 22, no_node), make(NodeKind::code_content, 0, 22, 0),
                  make(NodeKind::text, 2, 4, 1)});
  CHECK(bad.validate_coverage(padded).has_value());
  SyntaxTree good({make(NodeKind::document, 0, 22, no_node), make(NodeKind::code_content, 0, 22, 0),
                   make(NodeKind::block_continuation, 11, 13, 1)});
  CHECK_FALSE(good.validate_coverage(padded).has_value());

  const std::string bom = "\xEF\xBB\xBF\v\f a\n";
  SyntaxTree b({make(NodeKind::document, 0, bom.size(), no_node), make(NodeKind::paragraph, 6, 8, 0),
                make(NodeKind::text, 6, 7, 1)});
  CHECK_FALSE(b.validate_coverage(bom).has_value());
}

TEST_CASE("syntax: apply_tree_edits replaces a leaf range and moves the rest") {
  SyntaxTree t = two_paragraphs();
  TreeEdit e;
  e.first = 2;
  e.last = 3;
  e.nodes = {make(NodeKind::text, 0, 4, TreeEdit::old_parent_bit | 1),
             make(NodeKind::text, 5, 13, TreeEdit::old_parent_bit | 1)};
  e.ancestors = {{1, 14, false, false}};
  e.delta = 4;
  t.apply_tree_edits(std::span<const TreeEdit>(&e, 1), 26);
  CHECK(t.to_debug_string() ==
        "(document@0..26 (paragraph@0..14 (text@0..4) (text@5..13)) (paragraph@15..26 (text@15..25)))");
  CHECK_FALSE(t.validate(26).has_value());
  CHECK(t.node(1).next_sibling == 4);
}

TEST_CASE("syntax: apply_tree_edits moves the end of unlisted ancestors") {
  SyntaxTree t({make(NodeKind::document, 0, 10, no_node), make(NodeKind::block_quote, 0, 10, 0),
                make(NodeKind::paragraph, 2, 10, 1), make(NodeKind::text, 2, 9, 2)});
  TreeEdit e;
  e.first = 3;
  e.last = 4;
  e.nodes = {make(NodeKind::text, 2, 7, TreeEdit::old_parent_bit | 2)};
  e.delta = -2;
  t.apply_tree_edits(std::span<const TreeEdit>(&e, 1), 8);
  CHECK(t.to_debug_string() == "(document@0..8 (block_quote@0..8 (paragraph@2..8 (text@2..7))))");
}

TEST_CASE("syntax: apply_tree_edits with two edits and a removal") {
  SyntaxTree t = two_paragraphs();
  TreeEdit a;  
  a.first = 2;
  a.last = 3;
  a.nodes = {make(NodeKind::text, 0, 7, TreeEdit::old_parent_bit | 1)};
  a.ancestors = {{1, 8, false, false}};
  a.delta = -2;
  TreeEdit b;  
  b.first = 3;
  b.last = 5;
  b.delta = -11;
  std::vector<TreeEdit> edits{a, b};
  t.apply_tree_edits(edits, 9);
  CHECK(t.to_debug_string() == "(document@0..9 (paragraph@0..8 (text@0..7)))");
  CHECK_FALSE(t.validate(9).has_value());
}

TEST_CASE("syntax: apply_tree_edits counts an edit with first == last") {
  SyntaxTree t = two_paragraphs();
  TreeEdit a;  
  a.first = 2;
  a.last = 3;
  a.nodes = {make(NodeKind::text, 0, 4, TreeEdit::old_parent_bit | 1),
             make(NodeKind::text, 5, 13, TreeEdit::old_parent_bit | 1)};
  a.ancestors = {{1, 14, false, false}};
  a.delta = 4;
  TreeEdit b;  
  b.first = 4;
  b.last = 4;
  b.ancestors = {{3, 29, false, false}};
  b.delta = 3;
  std::vector<TreeEdit> edits{a, b};
  t.apply_tree_edits(edits, 29);
  CHECK(t.to_debug_string() ==
        "(document@0..29 (paragraph@0..14 (text@0..4) (text@5..13)) (paragraph@15..29 (text@18..28)))");
  CHECK_FALSE(t.validate(29).has_value());
}

TEST_CASE("syntax: apply_tree_edits remaps children of re-emitted blocks") {
  SyntaxTree t({make(NodeKind::document, 0, 8, no_node), make(NodeKind::list, 0, 8, 0),
                make(NodeKind::list_item, 0, 4, 1), make(NodeKind::paragraph, 2, 4, 2), make(NodeKind::text, 2, 3, 3),
                make(NodeKind::list_item, 4, 8, 1), make(NodeKind::paragraph, 6, 8, 5),
                make(NodeKind::text, 6, 7, 6)});
  TreeEdit e;
  e.first = 1;
  e.last = 5;
  Node list = make(NodeKind::list, 0, 9, no_node);
  list.tight = true;
  e.nodes = {list, make(NodeKind::list_item, 0, 5, 0), make(NodeKind::paragraph, 2, 5, 1),
             make(NodeKind::text, 2, 4, 2)};
  e.remap = {{1, 0}};
  e.delta = 1;
  t.apply_tree_edits(std::span<const TreeEdit>(&e, 1), 9);
  CHECK(t.to_debug_string() ==
        "(document@0..9 (list@0..9 tight (list_item@0..5 (paragraph@2..5 (text@2..4))) "
        "(list_item@5..9 (paragraph@7..9 (text@7..8)))))");
  CHECK_FALSE(t.validate(9).has_value());
}

TEST_CASE("syntax: apply_tree_edits remaps from a source before the range") {
  SyntaxTree t({make(NodeKind::document, 0, 12, no_node), make(NodeKind::block_quote, 0, 6, 0),
                make(NodeKind::paragraph, 6, 9, 0), make(NodeKind::paragraph, 9, 12, 1)});
  TreeEdit e;
  e.first = 2;
  e.last = 3;
  e.nodes = {make(NodeKind::block_quote, 6, 12, no_node)};
  e.remap = {{1, 0}};
  t.apply_tree_edits(std::span<const TreeEdit>(&e, 1), 12);
  CHECK(t.to_debug_string() ==
        "(document@0..12 (block_quote@0..6) (block_quote@6..12 (paragraph@9..12)))");
  CHECK_FALSE(t.validate(12).has_value());
}

TEST_CASE("syntax: apply_tree_edits remaps to an old node") {
  SyntaxTree t({make(NodeKind::document, 0, 12, no_node), make(NodeKind::block_quote, 0, 6, 0),
                make(NodeKind::block_quote, 6, 9, 0), make(NodeKind::paragraph, 9, 12, 2)});
  TreeEdit e;
  e.first = 2;
  e.last = 3;
  e.ancestors = {{1, 12, false, false, false}};
  e.remap = {{2, TreeEdit::old_parent_bit | 1}};
  t.apply_tree_edits(std::span<const TreeEdit>(&e, 1), 12);
  CHECK(t.to_debug_string() == "(document@0..12 (block_quote@0..12 (paragraph@9..12)))");
  CHECK_FALSE(t.validate(12).has_value());
}

TEST_CASE("syntax: apply_tree_edits rejects bad edits and keeps the tree") {
  SyntaxTree t = two_paragraphs();
  const std::string before = t.to_debug_string();
  TreeEdit e;
  e.first = 3;
  e.last = 2;
  CHECK_THROWS_AS(t.apply_tree_edits(std::span<const TreeEdit>(&e, 1), 22), std::invalid_argument);
  TreeEdit r;
  r.first = 1;
  r.last = 2;
  CHECK_THROWS_AS(t.apply_tree_edits(std::span<const TreeEdit>(&r, 1), 22), std::invalid_argument);
  TreeEdit root;
  root.first = 0;
  root.last = 1;
  CHECK_THROWS_AS(t.apply_tree_edits(std::span<const TreeEdit>(&root, 1), 22), std::invalid_argument);
  CHECK(t.to_debug_string() == before);
}
