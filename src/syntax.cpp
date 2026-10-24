
#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>
#include <string>
#include <string_view>

#include "lumos/parser.hpp"
#include "lumos/syntax.hpp"

namespace lumos {

std::string_view kind_name(NodeKind kind) noexcept {
  switch (kind) {
    case NodeKind::document: return "document";
    case NodeKind::paragraph: return "paragraph";
    case NodeKind::heading: return "heading";
    case NodeKind::block_quote: return "block_quote";
    case NodeKind::list: return "list";
    case NodeKind::list_item: return "list_item";
    case NodeKind::code_block: return "code_block";
    case NodeKind::html_block: return "html_block";
    case NodeKind::thematic_break: return "thematic_break";
    case NodeKind::table: return "table";
    case NodeKind::table_header_row: return "table_header_row";
    case NodeKind::table_delimiter_row: return "table_delimiter_row";
    case NodeKind::table_row: return "table_row";
    case NodeKind::table_cell: return "table_cell";
    case NodeKind::table_delimiter_cell: return "table_delimiter_cell";
    case NodeKind::link_reference_definition: return "link_reference_definition";
    case NodeKind::front_matter: return "front_matter";
    case NodeKind::emphasis: return "emphasis";
    case NodeKind::strong: return "strong";
    case NodeKind::strikethrough: return "strikethrough";
    case NodeKind::code_span: return "code_span";
    case NodeKind::link: return "link";
    case NodeKind::image: return "image";
    case NodeKind::autolink: return "autolink";
    case NodeKind::html_inline: return "html_inline";
    case NodeKind::hard_break: return "hard_break";
    case NodeKind::soft_break: return "soft_break";
    case NodeKind::entity: return "entity";
    case NodeKind::math: return "math";
    case NodeKind::text: return "text";
    case NodeKind::emphasis_delimiter: return "emphasis_delimiter";
    case NodeKind::heading_marker: return "heading_marker";
    case NodeKind::list_marker: return "list_marker";
    case NodeKind::task_marker: return "task_marker";
    case NodeKind::code_fence_delimiter: return "code_fence_delimiter";
    case NodeKind::info_string: return "info_string";
    case NodeKind::code_content: return "code_content";
    case NodeKind::code_span_delimiter: return "code_span_delimiter";
    case NodeKind::link_text: return "link_text";
    case NodeKind::link_delimiter: return "link_delimiter";
    case NodeKind::link_destination: return "link_destination";
    case NodeKind::link_title: return "link_title";
    case NodeKind::link_label: return "link_label";
    case NodeKind::escape: return "escape";
    case NodeKind::block_quote_marker: return "block_quote_marker";
    case NodeKind::block_continuation: return "block_continuation";
    case NodeKind::table_pipe: return "table_pipe";
    case NodeKind::math_delimiter: return "math_delimiter";
  }
  return "unknown";
}

bool is_block(NodeKind kind) noexcept {
  return kind <= NodeKind::front_matter;
}

bool is_marker(NodeKind kind) noexcept {
  return kind >= NodeKind::emphasis_delimiter && kind <= NodeKind::math_delimiter;
}

NodeClass node_class(NodeKind kind) noexcept {
  switch (kind) {
    case NodeKind::text:
    case NodeKind::code_content:
    case NodeKind::info_string:
    case NodeKind::html_block:
    case NodeKind::html_inline:
    case NodeKind::link_destination:
    case NodeKind::link_title:
    case NodeKind::link_label:
    case NodeKind::entity:
    case NodeKind::escape:
    case NodeKind::front_matter:
    case NodeKind::math:
      return NodeClass::content;
    case NodeKind::emphasis_delimiter:
    case NodeKind::heading_marker:
    case NodeKind::list_marker:
    case NodeKind::task_marker:
    case NodeKind::code_fence_delimiter:
    case NodeKind::code_span_delimiter:
    case NodeKind::link_delimiter:
    case NodeKind::block_quote_marker:
    case NodeKind::block_continuation:
    case NodeKind::table_pipe:
    case NodeKind::table_delimiter_cell:
    case NodeKind::thematic_break:
    case NodeKind::hard_break:
    case NodeKind::soft_break:
    case NodeKind::math_delimiter:
      return NodeClass::marker;
    default:
      return NodeClass::structure;
  }
}

std::string_view node_text(std::string_view source, const Node& node) noexcept {
  if (node.start >= source.size() || node.end <= node.start) return {};
  return source.substr(node.start, std::min(node.end, source.size()) - node.start);
}

SyntaxTree::SyntaxTree() : SyntaxTree(std::vector<Node>{}) {}

SyntaxTree::SyntaxTree(std::vector<Node> nodes) : nodes_(std::move(nodes)) {
  if (nodes_.empty()) {
    Node doc;
    doc.kind = NodeKind::document;
    nodes_.push_back(doc);
  }
  link();
}

void SyntaxTree::splice(NodeId first, NodeId last, std::vector<Node> nodes, std::ptrdiff_t delta,
                        std::size_t text_size) {
  const std::size_t old_count = last - first;
  const std::size_t new_count = nodes.size();
  const auto id_shift = static_cast<std::int64_t>(new_count) - static_cast<std::int64_t>(old_count);
  auto shift_id = [id_shift](NodeId id) { return static_cast<NodeId>(static_cast<std::int64_t>(id) + id_shift); };
  auto shift_pos = [delta](std::size_t pos) {
    return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(pos) + delta);
  };

  for (std::size_t i = 0; i < new_count; ++i) {
    nodes[i].subtree_end = static_cast<NodeId>(i + 1);
    nodes[i].next_sibling = no_node;
  }
  for (std::size_t i = new_count; i-- > 0;) {
    const NodeId p = nodes[i].parent;
    if (p != no_node && p < i) nodes[p].subtree_end = std::max(nodes[p].subtree_end, nodes[i].subtree_end);
  }
  for (std::size_t i = 0; i < new_count; ++i) {
    const NodeId j = nodes[i].subtree_end;
    if (j < new_count && nodes[j].parent == nodes[i].parent) nodes[i].next_sibling = static_cast<NodeId>(j + first);
    nodes[i].subtree_end += first;
    nodes[i].parent = nodes[i].parent == no_node ? 0 : nodes[i].parent + first;
  }

  const auto at = nodes_.begin() + static_cast<std::ptrdiff_t>(first);
  if (new_count > old_count) {
    nodes_.insert(at + static_cast<std::ptrdiff_t>(old_count), new_count - old_count, Node{});
  } else if (new_count < old_count) {
    nodes_.erase(at + static_cast<std::ptrdiff_t>(new_count), at + static_cast<std::ptrdiff_t>(old_count));
  }
  std::move(nodes.begin(), nodes.end(), nodes_.begin() + static_cast<std::ptrdiff_t>(first));

  const NodeId tail = static_cast<NodeId>(first + new_count);
  for (std::size_t i = tail; i < nodes_.size(); ++i) {
    Node& n = nodes_[i];
    n.start = shift_pos(n.start);
    n.end = shift_pos(n.end);
    if (n.parent >= last) n.parent = shift_id(n.parent);
    n.subtree_end = shift_id(n.subtree_end);
    if (n.next_sibling != no_node) n.next_sibling = shift_id(n.next_sibling);
  }

  Node& root = nodes_[0];
  root.start = 0;
  root.end = text_size;
  root.subtree_end = static_cast<NodeId>(nodes_.size());
  const auto n = static_cast<NodeId>(nodes_.size());
  for (NodeId c = first; c < tail;) {
    const NodeId next = nodes_[c].subtree_end;
    nodes_[c].next_sibling = next < n ? next : no_node;
    c = next;
  }
  if (first > 1) {
    NodeId prev = first - 1;
    while (nodes_[prev].parent != 0) prev = nodes_[prev].parent;
    nodes_[prev].next_sibling = first < n ? first : no_node;
  }
}

NodeId SyntaxTree::first_child(NodeId id) const noexcept {
  if (id >= nodes_.size()) return no_node;
  return nodes_[id].subtree_end > id + 1 ? id + 1 : no_node;
}

std::size_t SyntaxTree::depth(NodeId id) const noexcept {
  std::size_t d = 0;
  while (id < nodes_.size() && nodes_[id].parent != no_node) {
    id = nodes_[id].parent;
    ++d;
  }
  return d;
}

NodeId SyntaxTree::node_at(std::size_t offset) const noexcept {
  NodeId cur = 0;
  for (;;) {
    NodeId next = no_node;
    for (NodeId c = first_child(cur); c != no_node; c = nodes_[c].next_sibling) {
      if (nodes_[c].start > offset) break;
      if (nodes_[c].contains(offset)) {
        next = c;
        break;
      }
    }
    if (next == no_node) return cur;
    cur = next;
  }
}

namespace {

void append_attributes(const Node& n, std::string& out) {
  switch (n.kind) {
    case NodeKind::heading:
      out += " level=" + std::to_string(n.level);
      if (n.setext) out += " setext";
      break;
    case NodeKind::list:
      if (n.ordered) out += " ordered";
      if (n.tight) out += " tight";
      break;
    case NodeKind::task_marker:
      if (n.checked) out += " checked";
      break;
    case NodeKind::code_block:
      if (n.fenced) out += " fenced";
      break;
    case NodeKind::table_cell:
    case NodeKind::table_delimiter_cell:
      switch (n.align) {
        case Alignment::none: break;
        case Alignment::left: out += " align=left"; break;
        case Alignment::center: out += " align=center"; break;
        case Alignment::right: out += " align=right"; break;
      }
      break;
    default: break;
  }
  if (n.variant != 0) out += " variant=" + std::to_string(n.variant);
  if (n.virtual_spaces != 0) out += " vs=" + std::to_string(n.virtual_spaces);
}

bool is_gap_byte(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

}  

std::string SyntaxTree::to_debug_string() const {
  std::string out;
  std::vector<NodeId> open;  
  for (NodeId id = 0; id < nodes_.size(); ++id) {
    const Node& n = nodes_[id];
    while (!open.empty() && nodes_[open.back()].subtree_end <= id) {
      out += ')';
      open.pop_back();
    }
    if (id != 0) out += ' ';
    out += '(';
    out += kind_name(n.kind);
    out += '@';
    out += std::to_string(n.start);
    out += "..";
    out += std::to_string(n.end);
    append_attributes(n, out);
    open.push_back(id);
  }
  for (std::size_t i = 0; i < open.size(); ++i) out += ')';
  return out;
}

std::optional<std::string> SyntaxTree::validate(std::size_t text_size) const {
  auto where = [](std::size_t i, const Node& n) {
    return "node " + std::to_string(i) + " " + std::string(kind_name(n.kind)) + "@" +
           std::to_string(n.start) + ".." + std::to_string(n.end);
  };
  const Node& root = nodes_[0];
  if (root.kind != NodeKind::document || root.parent != no_node)
    return std::string("root is not a document");
  if (root.start != 0 || root.end != text_size)
    return "root range is not [0, " + std::to_string(text_size) + ")";
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    const Node& n = nodes_[i];
    if (n.start > n.end) return where(i, n) + ": start > end";
    if (n.end > text_size) return where(i, n) + ": end beyond text";
    if (i == 0) continue;
    if (n.parent >= i) return where(i, n) + ": parent not before node";
    const Node& p = nodes_[n.parent];
    if (i >= p.subtree_end) return where(i, n) + ": not in parent's subtree";
    if (n.start < p.start || n.end > p.end) return where(i, n) + ": outside parent " + where(n.parent, p);
    if (n.next_sibling != no_node) {
      const Node& s = nodes_[n.next_sibling];
      if (s.start < n.end) return where(i, n) + ": overlaps next sibling " + where(n.next_sibling, s);
    }
  }
  return std::nullopt;
}

std::optional<std::string> SyntaxTree::validate_coverage(std::string_view text) const {
  auto where = [](std::size_t i, const Node& n) {
    return "node " + std::to_string(i) + " " + std::string(kind_name(n.kind)) + "@" + std::to_string(n.start) + ".." +
           std::to_string(n.end);
  };
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    const Node& n = nodes_[i];
    if (n.end > text.size()) return where(i, n) + ": end beyond text";
    const NodeClass cls = node_class(n.kind);
    if (cls == NodeClass::marker && first_child(static_cast<NodeId>(i)) != no_node)
      return where(i, n) + ": marker with children";
    if (cls == NodeClass::content) {
      for (NodeId c : children(static_cast<NodeId>(i))) {
        const Node& cn = nodes_[c];
        if (cn.kind == NodeKind::block_continuation || (cn.kind == NodeKind::escape && cn.size() == 1)) continue;
        return where(i, n) + ": content node with child " + where(c, cn);
      }
      continue;
    }
    if (cls != NodeClass::structure) continue;
    std::size_t pos = n.start;
    if (i == 0 && text.substr(0, 3) == "\xEF\xBB\xBF") pos = 3;
    auto check_gap = [&](std::size_t from, std::size_t to) -> std::optional<std::string> {
      for (std::size_t b = from; b < to; ++b)
        if (!is_gap_byte(text[b]))
          return where(i, n) + ": byte " + std::to_string(b) + " is in no content or marker node";
      return std::nullopt;
    };
    for (NodeId c : children(static_cast<NodeId>(i))) {
      if (nodes_[c].start > pos)
        if (auto e = check_gap(pos, nodes_[c].start)) return e;
      pos = std::max(pos, nodes_[c].end);
    }
    if (n.end > pos)
      if (auto e = check_gap(pos, n.end)) return e;
  }
  return std::nullopt;
}

void SyntaxTree::link() noexcept {
  const std::size_t n = nodes_.size();
  for (std::size_t i = 0; i < n; ++i) {
    nodes_[i].subtree_end = static_cast<NodeId>(i + 1);
    nodes_[i].next_sibling = no_node;
  }
  for (std::size_t i = n; i-- > 1;) {
    const NodeId p = nodes_[i].parent;
    if (p < i) nodes_[p].subtree_end = std::max(nodes_[p].subtree_end, nodes_[i].subtree_end);
  }
  for (std::size_t i = 1; i < n; ++i) {
    const NodeId j = nodes_[i].subtree_end;
    if (j < n && nodes_[j].parent == nodes_[i].parent) nodes_[i].next_sibling = j;
  }
}

void SyntaxTree::apply_tree_edits(std::span<const TreeEdit> edits, std::size_t text_size) {
  const std::size_t n = nodes_.size();
  NodeId prev_last = 1;
  for (const TreeEdit& e : edits) {
    if (e.first < prev_last || e.last < e.first || e.last > n)
      throw std::invalid_argument("apply_tree_edits: edits not sorted, overlapping or out of range");
    for (const auto& r : e.remap) {
      if (r.old_id >= e.last) throw std::invalid_argument("apply_tree_edits: bad remap entry");
      if (r.new_index & TreeEdit::old_parent_bit) {
        if ((r.new_index & ~TreeEdit::old_parent_bit) >= e.first)
          throw std::invalid_argument("apply_tree_edits: bad remap entry");
      } else if (r.new_index >= e.nodes.size()) {
        throw std::invalid_argument("apply_tree_edits: bad remap entry");
      }
    }
    for (const auto& a : e.ancestors)
      if (a.id >= e.first) throw std::invalid_argument("apply_tree_edits: ancestor not before the range");
    prev_last = e.last;
  }

  std::vector<std::ptrdiff_t> delta_before(edits.size() + 1, 0);
  for (std::size_t k = 0; k < edits.size(); ++k) delta_before[k + 1] = delta_before[k] + edits[k].delta;
  auto edits_before = [&](NodeId id) {
    std::size_t lo = 0, hi = edits.size();
    while (lo < hi) {
      const std::size_t mid = (lo + hi) / 2;
      if (edits[mid].last <= id) lo = mid + 1;
      else hi = mid;
    }
    return lo;
  };
  auto shift = [](std::size_t pos, std::ptrdiff_t d) {
    return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(pos) + d);
  };

  std::vector<NodeId> map(n, no_node);  
  std::vector<Node> out;
  out.reserve(n);
  auto copy_old = [&](NodeId from, NodeId to) {
    if (from >= to) return;
    const std::size_t k = edits_before(from);
    const std::ptrdiff_t start_delta = delta_before[k];
    const bool has_next = k < edits.size();
    const NodeId next_last = has_next ? edits[k].last : 0;
    const std::size_t base = out.size();
    out.insert(out.end(), nodes_.begin() + from, nodes_.begin() + to);
    for (NodeId i = from; i < to; ++i) map[i] = static_cast<NodeId>(base + (i - from));
    for (NodeId i = from; i < to; ++i) {
      Node& node = out[base + (i - from)];
      const std::ptrdiff_t end_delta = has_next && node.subtree_end >= next_last
                                           ? delta_before[edits_before(node.subtree_end)]
                                           : start_delta;
      node.start = shift(node.start, start_delta);
      node.end = shift(node.end, end_delta);
      if (i != 0) {
        const NodeId p = node.parent < i ? map[node.parent] : no_node;
        if (p == no_node) throw std::invalid_argument("apply_tree_edits: kept node has a removed parent");
        node.parent = p;
      }
    }
  };

  NodeId pos = 0;
  for (const TreeEdit& e : edits) {
    copy_old(pos, e.first);
    const auto base = static_cast<NodeId>(out.size());
    out.insert(out.end(), e.nodes.begin(), e.nodes.end());
    for (std::size_t j = 0; j < e.nodes.size(); ++j) {
      Node& node = out[base + j];
      if (node.parent == no_node) {
        node.parent = 0;
      } else if (node.parent & TreeEdit::old_parent_bit) {
        const NodeId old = node.parent & ~TreeEdit::old_parent_bit;
        if (old >= e.first || map[old] == no_node) throw std::invalid_argument("apply_tree_edits: bad old parent");
        node.parent = map[old];
      } else {
        if (node.parent >= j) throw std::invalid_argument("apply_tree_edits: parent not before node");
        node.parent = base + node.parent;
      }
    }
    for (const auto& a : e.ancestors) {
      Node& node = out[map[a.id]];
      node.end = a.new_end;
      if (a.set_tight) node.tight = a.tight;
      node.last_line_blank = a.last_line_blank;
    }
    for (const auto& r : e.remap) {
      const bool is_old = r.new_index & TreeEdit::old_parent_bit;
      map[r.old_id] = is_old ? map[r.new_index & ~TreeEdit::old_parent_bit] : base + r.new_index;
    }
    pos = e.last;
  }
  copy_old(pos, static_cast<NodeId>(n));
  if (out.empty()) throw std::invalid_argument("apply_tree_edits: no root");
  out[0].start = 0;
  out[0].end = text_size;
  nodes_ = std::move(out);
  link();
}

Parser::~Parser() = default;

void Parser::parse(std::string_view text) { parse(StringSource(text)); }

void Parser::apply_edit(const Edit& edit, std::string_view new_text) { apply_edit(region_of(edit), StringSource(new_text)); }

}  
