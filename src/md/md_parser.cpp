#include "md/md_parser.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "lumos/log.hpp"
#include "md/block_parser.hpp"
#include "md/content.hpp"
#include "md/inline_parser.hpp"
#include "md/line_reader.hpp"
#include "wipe.hpp"

namespace lumos::md {

namespace {

std::string num(std::size_t v) { return std::to_string(v); }

std::vector<Segment> relative_segments(std::size_t leaf_start, const std::vector<Segment>& abs) {
  std::vector<Segment> rel;
  rel.reserve(abs.size());
  for (const Segment& s : abs) rel.push_back({s.start - leaf_start, s.end - leaf_start});
  return rel;
}

void append_relative_segments(ContentBuffer& buf, const TextSource& text, std::size_t leaf_start,
                               const std::vector<Segment>& rel) {
  for (const Segment& s : rel) {
    const std::size_t start = leaf_start + s.start;
    buf.append(start, text.slice(start, s.size()));
  }
}

struct LeafScratch {
  struct Open {
    NodeId new_index;
    std::size_t end;
  };
  ContentBuffer buf;
  std::vector<Node> prefixes;
  std::vector<InlineNode> inline_nodes;
  std::vector<NodeId> idx_map;
  std::vector<Open> stack;
};

std::vector<LabelId> parse_leaf_inline(InlineParser& parser, LabelLookup& lookup, const ContentBuffer& buf,
                                        const std::vector<Node>& prefix_nodes, NodeId leaf_index,
                                        std::vector<Node>& out, LeafScratch& scratch) {
  std::vector<InlineNode>& inline_nodes = scratch.inline_nodes;
  parser.parse(buf, lookup, inline_nodes);

  std::vector<NodeId>& idx_map = scratch.idx_map;
  idx_map.assign(inline_nodes.size(), no_node);
  std::vector<LeafScratch::Open>& stack = scratch.stack;
  stack.clear();
  std::size_t pcur = 0;
  auto flush_before = [&](std::size_t pos) {
    while (pcur < prefix_nodes.size() && prefix_nodes[pcur].start < pos) {
      while (!stack.empty() && stack.back().end <= prefix_nodes[pcur].start) stack.pop_back();
      Node p = prefix_nodes[pcur];
      p.parent = stack.empty() ? leaf_index : stack.back().new_index;
      out.push_back(p);
      ++pcur;
    }
  };
  for (std::size_t k = 0; k < inline_nodes.size(); ++k) {
    const Node& n = inline_nodes[k].node;
    flush_before(n.start);
    Node out_node = n;
    out_node.parent = n.parent == no_node ? leaf_index : idx_map[n.parent];
    idx_map[k] = static_cast<NodeId>(out.size());
    out.push_back(out_node);
    stack.push_back({idx_map[k], n.end});
  }
  flush_before(SIZE_MAX);
  return lookup.take();
}

void flatten_prefixes(const std::vector<Node>& nodes, std::size_t first, std::size_t last, std::vector<Node>& out) {
  out.clear();
  for (std::size_t k = first; k < last; ++k) {
    const Node& n = nodes[k];
    if (n.kind == NodeKind::block_continuation || n.kind == NodeKind::heading_marker ||
        (n.kind == NodeKind::escape && n.size() == 1))
      out.push_back(n);
  }
}

std::size_t run_subtree_end(const std::vector<Node>& nodes, std::size_t i) {
  std::size_t j = i + 1;
  while (j < nodes.size()) {
    const NodeId p = nodes[j].parent;
    if (p == no_node || (p & TreeEdit::old_parent_bit) != 0 || p < static_cast<NodeId>(i)) break;
    ++j;
  }
  return j;
}

void merge_inline(const Features& features, const TextSource& text, RefIndex& refs, BlockOutput& out,
                  std::span<const RefIndex::LeafQueries> taken_leaves, std::span<const LabelId> flipped,
                  std::vector<RefIndex::LeafQueries>& leaf_queries) {
  if (out.leaves.empty() && taken_leaves.empty()) return;
  std::vector<Node> fresh;
  fresh.reserve(out.nodes.size());
  struct Shift {
    std::size_t from;
    std::ptrdiff_t by;
  };
  std::vector<Shift> shifts{{0, 0}};
  auto remap_parent = [&](NodeId p) -> NodeId {
    if (p == no_node || (p & TreeEdit::old_parent_bit) != 0) return p;
    auto it = shifts.end() - 1;
    if (p < it->from) {
      it = std::upper_bound(shifts.begin(), shifts.end(), static_cast<std::size_t>(p),
                            [](std::size_t v, const Shift& s) { return v < s.from; }) -
           1;
    }
    return static_cast<NodeId>(static_cast<std::ptrdiff_t>(p) + it->by);
  };
  auto push_leaf = [&](std::size_t i) -> NodeId {
    Node leaf_node = out.nodes[i];
    leaf_node.parent = remap_parent(leaf_node.parent);
    fresh.push_back(leaf_node);
    return static_cast<NodeId>(fresh.size() - 1);
  };
  LabelLookup lookup(refs);
  InlineParser parser(features);
  LeafScratch scratch;
  ContentBuffer& buf = scratch.buf;
  std::vector<Node>& prefixes = scratch.prefixes;
  std::size_t leaf_cursor = 0;
  std::size_t taken_cursor = 0;
  const std::size_t n = out.nodes.size();
  std::size_t i = 0;
  while (i < n) {
    const std::size_t limit = leaf_cursor < out.leaves.size() ? out.leaves[leaf_cursor].node : n;
    std::size_t e = i;
    bool dependent = false;
    for (; e < limit; ++e) {
      if (taken_cursor >= taken_leaves.size()) {
        e = limit;
        break;
      }
      const Node& node = out.nodes[e];
      if ((node.kind != NodeKind::paragraph && node.kind != NodeKind::heading &&
           node.kind != NodeKind::table_cell) ||
          node.start != taken_leaves[taken_cursor].start) {
        continue;
      }
      const RefIndex::LeafQueries& carried = taken_leaves[taken_cursor];
      dependent = std::any_of(carried.labels.begin(), carried.labels.end(),
                              [&](LabelId id) { return std::binary_search(flipped.begin(), flipped.end(), id); });
      if (dependent) break;
      leaf_queries.push_back(carried);
      ++taken_cursor;
    }
    const std::size_t base = fresh.size();
    fresh.insert(fresh.end(), out.nodes.begin() + static_cast<std::ptrdiff_t>(i),
                 out.nodes.begin() + static_cast<std::ptrdiff_t>(e));
    if (shifts.size() > 1) {
      for (std::size_t k = base; k < fresh.size(); ++k) fresh[k].parent = remap_parent(fresh[k].parent);
    }
    i = e;
    if (i >= n) break;

    if (dependent) {
      const RefIndex::LeafQueries& carried = taken_leaves[taken_cursor];
      ++taken_cursor;
      const std::size_t subtree_end = run_subtree_end(out.nodes, i);
      flatten_prefixes(out.nodes, i + 1, subtree_end, prefixes);
      buf.clear();
      append_relative_segments(buf, text, out.nodes[i].start, carried.segments);
      const NodeId leaf_new_index = push_leaf(i);
      std::vector<LabelId> asked = parse_leaf_inline(parser, lookup, buf, prefixes, leaf_new_index, fresh, scratch);
      if (!asked.empty()) {
        RefIndex::LeafQueries q;
        q.start = out.nodes[i].start;
        q.labels = std::move(asked);
        q.segments = carried.segments;
        leaf_queries.push_back(std::move(q));
      }
      i = subtree_end;
    } else {
      const BlockOutput::Leaf& leaf = out.leaves[leaf_cursor];
      ++leaf_cursor;
      std::size_t j = i + 1;
      prefixes.clear();
      while (j < n && out.nodes[j].parent == static_cast<NodeId>(i)) {
        prefixes.push_back(out.nodes[j]);
        ++j;
      }
      buf.clear();
      for (const Segment& seg : leaf.segments) buf.append(seg.start, text.slice(seg.start, seg.size()));
      const NodeId leaf_new_index = push_leaf(i);
      std::vector<LabelId> asked = parse_leaf_inline(parser, lookup, buf, prefixes, leaf_new_index, fresh, scratch);
      if (!asked.empty()) {
        RefIndex::LeafQueries q;
        q.start = out.nodes[i].start;
        q.labels = std::move(asked);
        q.segments = relative_segments(q.start, leaf.segments);
        leaf_queries.push_back(std::move(q));
      }
      i = j;
    }
    shifts.push_back({i, static_cast<std::ptrdiff_t>(fresh.size()) - static_cast<std::ptrdiff_t>(i)});
  }
  out.nodes = std::move(fresh);
}

const RefIndex::LeafQueries* find_leaf_queries(std::span<const RefIndex::LeafQueries> leaves, std::size_t start) {
  const auto it = std::lower_bound(leaves.begin(), leaves.end(), start,
                                    [](const RefIndex::LeafQueries& q, std::size_t v) { return q.start < v; });
  return it != leaves.end() && it->start == start ? &*it : nullptr;
}

TreeEdit reparse_outside_leaf(const Features& features, const TextSource& text, RefIndex& refs,
                              const SyntaxTree& tree, NodeId leaf, std::ptrdiff_t shift) {
  const Node leaf_node = tree.node(leaf);
  const NodeId end = leaf_node.subtree_end;
  std::vector<Node> copied;
  copied.reserve(end - leaf - 1);
  for (NodeId id = leaf + 1; id < end; ++id) {
    Node n = tree.node(id);
    n.start = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(n.start) + shift);
    n.end = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(n.end) + shift);
    copied.push_back(std::move(n));
  }
  LeafScratch scratch;
  std::vector<Node>& prefixes = scratch.prefixes;
  flatten_prefixes(copied, 0, copied.size(), prefixes);

  const std::size_t leaf_new_start = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(leaf_node.start) + shift);
  const RefIndex::LeafQueries* stored = find_leaf_queries(refs.leaves(), leaf_new_start);
  ContentBuffer buf;
  if (stored != nullptr) append_relative_segments(buf, text, leaf_new_start, stored->segments);

  LabelLookup lookup(refs);
  InlineParser parser(features);
  TreeEdit edit;
  edit.first = leaf + 1;
  edit.last = end;
  edit.delta = 0;
  std::vector<LabelId> asked =
      parse_leaf_inline(parser, lookup, buf, prefixes, TreeEdit::old_parent_bit | leaf, edit.nodes, scratch);
  refs.set_leaf_labels(leaf_new_start, std::move(asked));
  return edit;
}

std::size_t bom_size(const TextSource& text) {
  if (text.size() < 3) return 0;
  return text.slice(0, 3) == "\xEF\xBB\xBF" ? 3 : 0;
}

bool only_spaces_tabs(std::string_view s) noexcept {
  for (char c : s)
    if (c != ' ' && c != '\t') return false;
  return true;
}

std::optional<std::string_view> front_matter_opener(const Line& first) noexcept {
  if (first.text.size() < 3 || first.ending == 0) return std::nullopt;
  const std::string_view head = first.text.substr(0, 3);
  if (head != "---" && head != "+++") return std::nullopt;
  return only_spaces_tabs(first.text.substr(3)) ? std::make_optional(head) : std::nullopt;
}

bool is_front_matter_closer(const Line& line, std::string_view opener) noexcept {
  return line.text.size() >= 3 && line.text.substr(0, 3) == opener && only_spaces_tabs(line.text.substr(3));
}

struct FrontMatter {
  std::size_t end = 0;    
  std::size_t lines = 0;  
};

std::optional<FrontMatter> scan_front_matter(const TextSource& text) {
  LineReader reader(text);
  const Line first = reader.read(bom_size(text));
  const std::optional<std::string_view> opener = front_matter_opener(first);
  if (!opener) return std::nullopt;
  std::size_t offset = first.end();
  std::size_t index = 1;
  while (offset < text.size()) {
    const Line line = reader.read(offset);
    if (is_front_matter_closer(line, *opener)) return FrontMatter{line.end(), index + 1};
    offset = line.end();
    ++index;
  }
  return std::nullopt;
}

void emit_front_matter(BlockOutput& out, StateArena& arena, const TextSource& text, const FrontMatter& fm,
                       std::uint32_t document_parent) {
  OpenBlock entry;
  entry.kind = OpenKind::front_matter;
  const StateId fm_state = arena.push(document_state, entry);
  LineReader reader(text);
  std::size_t off = bom_size(text);
  for (std::size_t i = 0; i < fm.lines; ++i) {
    out.line_starts.push_back(off);
    out.line_states.push_back(i == 0 ? document_state : fm_state);
    off = reader.read(off).end();
  }
  Node n;
  n.kind = NodeKind::front_matter;
  n.start = bom_size(text);
  n.end = fm.end;
  n.parent = document_parent;
  out.nodes.push_back(n);
}

struct IncrementalMismatch : std::runtime_error {
  using std::runtime_error::runtime_error;
};

NodeId first_node_at_or_after(const SyntaxTree& t, std::size_t offset) {
  const auto nodes = t.nodes();
  std::size_t lo = 0, hi = nodes.size();
  while (lo < hi) {
    const std::size_t mid = (lo + hi) / 2;
    if (nodes[mid].start < offset) lo = mid + 1;
    else hi = mid;
  }
  return static_cast<NodeId>(lo);
}

bool line_blank(const Line& line) {
  LineCursor cur(line.text);
  return cur.blank();
}

bool is_cmark_block(NodeKind kind) noexcept { return is_block(kind) && kind != NodeKind::link_reference_definition; }

bool old_line_starts_definition(const SyntaxTree& t, const LineTable& lines, std::size_t line,
                                std::size_t old_size) {
  const std::size_t start = lines.start(line);
  const std::size_t end = line + 1 < lines.size() ? lines.start(line + 1) : old_size;
  NodeId n = first_node_at_or_after(t, start);
  while (n < t.size() && t.node(n).start < end && !is_block(t.node(n).kind)) ++n;
  return n < t.size() && t.node(n).start < end && t.node(n).kind == NodeKind::link_reference_definition;
}

bool old_ends_with_blank_line(const SyntaxTree& t, NodeId id) {
  for (;;) {
    const Node& n = t.node(id);
    if (n.kind != NodeKind::list && n.kind != NodeKind::list_item) return n.last_line_blank;
    NodeId last = no_node;
    for (NodeId c : t.children(id))
      if (is_cmark_block(t.node(c).kind)) last = c;
    if (last == no_node) return n.last_line_blank;
    id = last;
  }
}

bool old_item_llb(const SyntaxTree& t, NodeId item_id) {
  const Node& item = t.node(item_id);
  if (item.last_line_blank) return true;
  for (NodeId c : t.children(item_id)) {
    if (!is_cmark_block(t.node(c).kind)) continue;
    if (old_ends_with_blank_line(t, c)) return true;
  }
  return false;
}

NodeId next_block_sibling(const SyntaxTree& t, NodeId id) {
  NodeId n = t.node(id).next_sibling;
  while (n != no_node && !is_block(t.node(n).kind)) n = t.node(n).next_sibling;
  return n;
}

std::vector<std::string> defined_labels(const RefIndex& refs) {
  std::vector<std::string> out;
  for (const RefIndex::Definition& d : refs.definitions()) out.emplace_back(refs.label(d.label));
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::vector<std::string> flip_texts(const RefIndex& refs, const std::vector<LabelId>& ids) {
  std::vector<std::string> out;
  out.reserve(ids.size());
  for (LabelId id : ids) out.emplace_back(refs.label(id));
  std::sort(out.begin(), out.end());
  return out;
}

struct TakeAttempt {
  std::size_t next_offset = 0;
  std::size_t taken_lines = 0;
  std::optional<std::size_t> guard_old_line;
};

std::optional<TakeAttempt> try_take(BlockParser& blocks, const SyntaxTree& tree, const LineTable& lines,
                                     const TextRegion& region, std::ptrdiff_t delta, std::size_t r_line,
                                     std::size_t old_size, std::size_t offset, const RefIndex& refs,
                                     std::vector<RefIndex::Definition>& taken_defs,
                                     std::vector<RefIndex::LeafQueries>& taken_leaves) {
  const std::size_t p = blocks.open_leaf_index();
  const bool para_open = p != static_cast<std::size_t>(no_index);
  const std::size_t max_d = para_open ? (p >= 2 ? p - 2 : 0) : blocks.open_depth();
  if (max_d < 1) return std::nullopt;

  std::size_t old_line_l;
  bool after_edit;
  if (offset < region.start) {
    old_line_l = lines.line_of(offset);
    after_edit = false;
  } else if (offset >= region.new_end) {
    const auto old_off_signed = static_cast<std::ptrdiff_t>(offset) - delta;
    if (old_off_signed < 0) return std::nullopt;
    const auto old_offset = static_cast<std::size_t>(old_off_signed);
    if (old_offset > old_size) return std::nullopt;
    old_line_l = lines.line_of(old_offset);
    after_edit = true;
  } else {
    return std::nullopt;
  }
  if (lines.start(old_line_l) != (after_edit ? static_cast<std::size_t>(static_cast<std::ptrdiff_t>(offset) - delta)
                                             : offset)) {
    return std::nullopt;
  }

  if (blocks.state() != lines.state(old_line_l)) return std::nullopt;

  const std::size_t line_start = lines.start(old_line_l);
  const std::size_t line_end = old_line_l + 1 < lines.size() ? lines.start(old_line_l + 1) : old_size;
  const NodeId a = first_node_at_or_after(tree, line_start);
  NodeId b_id = a;
  while (b_id < tree.size() && tree.node(b_id).start < line_end && !is_block(tree.node(b_id).kind)) ++b_id;
  if (b_id >= tree.size() || tree.node(b_id).start >= line_end) return std::nullopt;

  const NodeId old_c = tree.node(b_id).parent;
  if (old_c == no_node) return std::nullopt;
  const std::size_t d = tree.depth(old_c);
  if (d < 1 || d > max_d) return std::nullopt;

  if (tree.node(b_id).kind == NodeKind::link_reference_definition) return std::nullopt;
  const std::size_t r_start = r_line < lines.size() ? lines.start(r_line) : old_size;
  auto same_side = [&](NodeId id) { return after_edit || tree.node(id).start < r_start; };
  auto is_definition = [&](NodeId id) { return tree.node(id).kind == NodeKind::link_reference_definition; };
  NodeId bm = b_id;
  NodeId bm1 = next_block_sibling(tree, bm);
  if (bm1 == no_node || !same_side(bm1) || is_definition(bm1)) return std::nullopt;
  for (;;) {
    const NodeId after_bm1 = next_block_sibling(tree, bm1);
    if (after_bm1 == no_node || !same_side(after_bm1) || is_definition(after_bm1)) break;
    bm = bm1;
    bm1 = after_bm1;
  }

  const std::size_t bm1_line = lines.line_of(tree.node(bm1).start);
  const std::size_t bm1_line_start = lines.start(bm1_line);
  const NodeId b = first_node_at_or_after(tree, bm1_line_start);
  const std::span<const Node> old_nodes = tree.nodes().subspan(a, b - a);
  for (std::size_t k = 0; k < old_nodes.size(); ++k) {
    const NodeId parent = old_nodes[k].parent;
    if (parent != old_c && (parent < a || parent - a >= k)) return std::nullopt;
  }
  const std::ptrdiff_t shift = after_edit ? delta : 0;
  refs.definitions_in(line_start, bm1_line_start, shift, taken_defs);
  refs.leaves_in(line_start, bm1_line_start, shift, taken_leaves);

  std::vector<BlockParser::TakenSibling> siblings;
  for (NodeId sib = b_id;; sib = next_block_sibling(tree, sib)) {
    const Node& sib_node = tree.node(sib);
    const bool llb =
        sib_node.kind == NodeKind::list_item ? old_item_llb(tree, sib) : old_ends_with_blank_line(tree, sib);
    siblings.push_back({static_cast<std::size_t>(sib - a), llb});
    if (sib == bm) break;
  }

  std::vector<BlockParser::TakenLine> taken_lines;
  taken_lines.reserve(bm1_line - old_line_l);
  for (std::size_t i = old_line_l; i < bm1_line; ++i) {
    const std::size_t shifted = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(lines.start(i)) + shift);
    taken_lines.push_back({shifted, lines.state(i)});
  }

  const std::size_t content_end_new = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(tree.node(bm).end) + shift);
  const StateId next_line_state = lines.state(bm1_line);
  const std::size_t taken_count = taken_lines.size();
  blocks.take(d, old_nodes, a, old_c, shift, siblings, taken_lines, content_end_new, next_line_state);

  TakeAttempt attempt;
  attempt.next_offset = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(bm1_line_start) + shift);
  attempt.taken_lines = taken_count;
  if (bm1_line + 1 < lines.size()) attempt.guard_old_line = bm1_line + 1;
  return attempt;
}

}  

MarkdownParser::MarkdownParser(const Dialect& dialect)
    : dialect_(dialect), features_(Features::from(dialect)), arena_(std::make_unique<StateArena>()) {}

void MarkdownParser::full_parse(const TextSource& text) {
  const auto t0 = std::chrono::steady_clock::now();
  const std::size_t size = text.size();
  const bool debug = log_enabled(LogLevel::debug);
  if (debug) log(LogLevel::debug, "parser", "parse_start", {{"bytes", num(size)}, {"backend", "lumos"}});
  const std::size_t old_size = tree_.node(0).end;
  arena_ = std::make_unique<StateArena>();
  BlockParser blocks(features_, *arena_);
  BlockOutput out;
  std::optional<FrontMatter> fm;
  if (features_.front_matter) fm = scan_front_matter(text);
  blocks.begin(size, out, text, fm ? fm->lines : 0);
  if (fm) emit_front_matter(out, *arena_, text, *fm, 0);
  LineReader reader(text);
  for (std::size_t offset = fm ? fm->end : bom_size(text); offset < size;) {
    const Line line = reader.read(offset);
    blocks.feed(line);
    offset = line.end();
  }
  blocks.finish();
  lines_.clear();
  for (std::size_t i = 0; i < out.line_starts.size(); ++i) lines_.push(out.line_starts[i], out.line_states[i]);
  const std::vector<std::string> before_defined = defined_labels(refs_);
  refs_.clear();
  std::vector<RefIndex::Definition> fresh;
  fresh.reserve(out.definitions.size());
  for (const BlockOutput::Definition& d : out.definitions) fresh.push_back({d.start, refs_.intern(d.label)});
  std::vector<LabelId> flipped;
  refs_.replace_definitions(0, 0, size, fresh, flipped);
  const std::vector<std::string> after_defined = defined_labels(refs_);
  last_flipped_.clear();
  std::set_symmetric_difference(before_defined.begin(), before_defined.end(), after_defined.begin(),
                                after_defined.end(), std::back_inserter(last_flipped_));
  std::vector<RefIndex::LeafQueries> leaf_queries;
  merge_inline(features_, text, refs_, out, {}, {}, leaf_queries);
  refs_.replace_leaves(0, 0, size, std::move(leaf_queries));
  tree_ = SyntaxTree(std::move(out.nodes));
  changes_.assign(1, TextRegion{0, old_size, size});
  arena_size_at_last_compact_ = arena_->size();
  stats_ = RunStats{false, 0, 0, lines_.size(), 0};
  const std::size_t labels_before_compact = refs_.label_count();
  if (refs_.maybe_compact() && debug)
    log(LogLevel::debug, "parser", "refs_compact",
        {{"labels_before", num(labels_before_compact)}, {"labels_after", num(refs_.label_count())}});
  if (debug) {
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0);
    log(LogLevel::debug, "parser", "parse_end",
        {{"nodes", num(tree_.size())}, {"lines", num(lines_.size())}, {"states", num(arena_->size())},
         {"us", num(static_cast<std::size_t>(us.count()))}});
  }
}

void MarkdownParser::try_apply_edit(const TextRegion& region, const TextSource& text) {
  const auto t0 = std::chrono::steady_clock::now();
  const bool debug = log_enabled(LogLevel::debug);

  const std::size_t old_size = tree_.node(0).end;
  const std::size_t new_size = text.size();
  const std::ptrdiff_t delta =
      static_cast<std::ptrdiff_t>(region.new_end) - static_cast<std::ptrdiff_t>(region.old_end);

  const std::size_t r_line = lines_.line_of(std::min(region.start, old_size));
  LineReader reader(text);
  std::size_t l = 0;
  bool restart_at_r = false;
  if (r_line > 0 && r_line < lines_.size() && lines_.state(r_line) == document_state) {
    const std::size_t old_start = lines_.start(r_line);
    const std::size_t old_end = r_line + 1 < lines_.size() ? lines_.start(r_line + 1) : old_size;
    const NodeId first = first_node_at_or_after(tree_, old_start);
    restart_at_r = first < tree_.size() && tree_.node(first).start < old_end && old_start < new_size &&
                   !old_line_starts_definition(tree_, lines_, r_line, old_size);
    if (restart_at_r) {
      const Line line = reader.read(old_start);
      LineCursor cur(line.text);
      restart_at_r = !cur.blank() && line.text[cur.first_nonspace()] != '[';
    }
  }
  if (restart_at_r) l = r_line;
  for (std::size_t i = restart_at_r ? 0 : r_line; i-- > 0;) {
    if (lines_.state(i) != document_state) continue;
    if (line_blank(reader.read(lines_.start(i)))) continue;
    if (old_line_starts_definition(tree_, lines_, i, old_size)) continue;
    l = i;
    break;
  }
  bool front_matter_reason = false;
  const bool old_has_front_matter = tree_.size() > 1 && tree_.node(1).kind == NodeKind::front_matter;
  if (features_.front_matter && l != 0 && r_line > 0 && !old_has_front_matter) {
    const Line first = reader.read(bom_size(text));
    if (const std::optional<std::string_view> opener = front_matter_opener(first)) {
      std::size_t off = lines_.start(l);
      while (off < new_size) {
        const Line ln = reader.read(off);
        if (is_front_matter_closer(ln, *opener)) {
          front_matter_reason = true;
          break;
        }
        if (region.new_end <= ln.end()) break;
        off = ln.end();
      }
    }
  }
  if (front_matter_reason) l = 0;
  const std::size_t R = lines_.start(l);

  std::optional<FrontMatter> fm;
  if (features_.front_matter && l == 0) fm = scan_front_matter(text);
  if (fm && debug) {
    const std::size_t end_old = old_has_front_matter ? tree_.node(1).end : 0;
    log(LogLevel::debug, "parser", "front_matter", {{"end_old", num(end_old)}, {"end_new", num(fm->end)}});
  }

  BlockParser blocks(features_, *arena_);
  BlockOutput out;
  blocks.begin_run(new_size, out, fm ? fm->lines : l, text);
  if (fm) emit_front_matter(out, *arena_, text, *fm, no_node);
  std::size_t offset = fm ? fm->end : R;
  bool stopped = false;
  std::size_t stop_old_line = 0;
  std::size_t fed_lines = 0;
  std::size_t taken_lines = 0;
  std::size_t takes = 0;
  std::vector<RefIndex::Definition> taken_defs;
  std::vector<RefIndex::LeafQueries> taken_leaves;
  std::optional<std::size_t> guard_old_line;
  while (offset < new_size) {
    const Line line = reader.read(offset);
    if (offset >= region.new_end) {
      const auto old_off_signed = static_cast<std::ptrdiff_t>(offset) - delta;
      if (old_off_signed >= 0) {
        const auto old_offset = static_cast<std::size_t>(old_off_signed);
        if (old_offset <= old_size) {
          const std::size_t old_line = lines_.line_of(old_offset);
          if (old_line < lines_.size() && lines_.start(old_line) == old_offset) {
            if (old_line > l && lines_.state(old_line) == document_state && blocks.state() == document_state &&
                !line_blank(line)) {
              if (!old_line_starts_definition(tree_, lines_, old_line, old_size)) {
                stopped = true;
                stop_old_line = old_line;
                break;
              }
            }
          }
        }
      }
    }
    if (auto attempt = try_take(blocks, tree_, lines_, region, delta, r_line, old_size, offset, refs_, taken_defs,
                                 taken_leaves)) {
      offset = attempt->next_offset;
      taken_lines += attempt->taken_lines;
      ++takes;
      guard_old_line = attempt->guard_old_line;
      continue;
    }
    blocks.feed(line);
    ++fed_lines;
    if (guard_old_line) {
      if (blocks.state() != lines_.state(*guard_old_line)) throw IncrementalMismatch("take_guard");
      guard_old_line.reset();
    }
    offset = line.end();
  }
  const std::size_t J = offset;
  const std::size_t J_old = stopped ? lines_.start(stop_old_line) : old_size;

  blocks.finish_run();

  std::vector<RefIndex::Definition> fresh_defs;
  fresh_defs.reserve(out.definitions.size() + taken_defs.size());
  for (const BlockOutput::Definition& d : out.definitions) fresh_defs.push_back({d.start, refs_.intern(d.label)});
  fresh_defs.insert(fresh_defs.end(), taken_defs.begin(), taken_defs.end());
  std::sort(fresh_defs.begin(), fresh_defs.end(),
            [](const RefIndex::Definition& x, const RefIndex::Definition& y) { return x.start < y.start; });
  std::vector<LabelId> flipped;
  refs_.replace_definitions(R, J_old, J, fresh_defs, flipped);
  std::sort(flipped.begin(), flipped.end());
  last_flipped_ = flip_texts(refs_, flipped);

  std::vector<std::size_t> outside_old_starts;
  if (!flipped.empty()) refs_.dependents(flipped, R, J_old, outside_old_starts);

  std::vector<RefIndex::LeafQueries> leaf_queries;
  merge_inline(features_, text, refs_, out, taken_leaves, flipped, leaf_queries);
  refs_.replace_leaves(R, J_old, J, std::move(leaf_queries));

  std::vector<TreeEdit> edits;
  edits.reserve(outside_old_starts.size() + 1);
  std::vector<TextRegion> dependent_regions;
  dependent_regions.reserve(outside_old_starts.size());
  for (const std::size_t old_start : outside_old_starts) {
    const std::ptrdiff_t shift = old_start < R ? 0 : delta;
    NodeId leaf = std::max<NodeId>(1, first_node_at_or_after(tree_, old_start));
    while (leaf < tree_.size() && tree_.node(leaf).start == old_start && tree_.node(leaf).kind != NodeKind::paragraph &&
           tree_.node(leaf).kind != NodeKind::heading && tree_.node(leaf).kind != NodeKind::table_cell)
      ++leaf;
    if (leaf >= tree_.size() || tree_.node(leaf).start != old_start) throw IncrementalMismatch("dependent_leaf");
    const std::size_t new_start = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(old_start) + shift);
    const std::size_t new_end = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(tree_.node(leaf).end) + shift);
    edits.push_back(reparse_outside_leaf(features_, text, refs_, tree_, leaf, shift));
    dependent_regions.push_back({new_start, new_end, new_end});
  }

  if (debug && !flipped.empty()) {
    std::size_t taken_dependent_count = 0;
    for (const RefIndex::LeafQueries& q : taken_leaves) {
      if (std::any_of(q.labels.begin(), q.labels.end(),
                       [&](LabelId id) { return std::binary_search(flipped.begin(), flipped.end(), id); }))
        ++taken_dependent_count;
    }
    log(LogLevel::debug, "parser", "refs_flipped",
        {{"labels", num(flipped.size())},
         {"leaves", num(outside_old_starts.size() + taken_dependent_count)},
         {"taken_leaves", num(taken_dependent_count)}});
  }

  TreeEdit edit;
  edit.first = std::max<NodeId>(1, first_node_at_or_after(tree_, R));
  edit.last = stopped ? std::max<NodeId>(1, first_node_at_or_after(tree_, J_old)) : static_cast<NodeId>(tree_.size());
  edit.nodes = std::move(out.nodes);
  edit.delta = delta;
  edits.push_back(std::move(edit));

  std::sort(edits.begin(), edits.end(), [](const TreeEdit& x, const TreeEdit& y) { return x.first < y.first; });

  changes_.assign(1, TextRegion{R, J_old, J});
  changes_.insert(changes_.end(), dependent_regions.begin(), dependent_regions.end());
  std::sort(changes_.begin(), changes_.end(),
            [](const TextRegion& x, const TextRegion& y) { return x.start < y.start; });

  tree_.apply_tree_edits(edits, new_size);

  lines_.replace(l, stopped ? stop_old_line : lines_.size(), out.line_starts, out.line_states, delta);
  stats_ = RunStats{true, l, l + fed_lines, fed_lines, takes};
  if (arena_->size() > 2 * std::max<std::size_t>(arena_size_at_last_compact_, 1)) {
    const std::size_t before = arena_->size();
    const std::vector<StateId> map = arena_->compact(lines_.states());
    lines_.remap_states(map);
    arena_size_at_last_compact_ = arena_->size();
    if (debug)
      log(LogLevel::debug, "parser", "arena_compact",
          {{"entries_before", num(before)}, {"entries_after", num(arena_->size())}});
  }

  const std::size_t labels_before_compact = refs_.label_count();
  if (refs_.maybe_compact() && debug)
    log(LogLevel::debug, "parser", "refs_compact",
        {{"labels_before", num(labels_before_compact)}, {"labels_after", num(refs_.label_count())}});

  if (debug) {
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0);
    log(LogLevel::debug, "parser", "reparse",
        {{"restart", num(R)},
         {"reason", front_matter_reason ? "front_matter" : "document_state"},
         {"stop", num(J)},
         {"stop_reason", stopped ? "document_state" : "end_of_text"},
         {"lines", num(fed_lines)},
         {"taken_lines", num(taken_lines)},
         {"takes", num(takes)},
         {"us", num(static_cast<std::size_t>(us.count()))}});
  }
}

void MarkdownParser::parse(const TextSource& text) { full_parse(text); }

void MarkdownParser::wipe() {
  refs_.wipe();
  for (std::string& label : last_flipped_) wipe_string(label);
  last_flipped_.clear();
  full_parse(StringSource(""));
}

void MarkdownParser::apply_edit(const TextRegion& region, const TextSource& text) {
  if (lines_.size() == 0) {
    if (log_enabled(LogLevel::debug)) log(LogLevel::debug, "parser", "reparse", {{"reason", "empty_text"}});
    full_parse(text);
    return;
  }
  try {
    try_apply_edit(region, text);
  } catch (const IncrementalMismatch& e) {
    ++fallback_count_;
    last_fallback_reason_ = e.what();
    if (log_enabled(LogLevel::debug))
      log(LogLevel::debug, "parser", "fallback",
          {{"reason", "region_mismatch"}, {"use", "full_parse"}, {"what", e.what()}});
    full_parse(text);
  }
}

std::unique_ptr<Parser> make_markdown_parser(const Dialect& dialect) {
  return std::make_unique<MarkdownParser>(dialect);
}

}  

namespace lumos {

std::unique_ptr<Parser> make_parser(const Dialect& dialect) { return md::make_markdown_parser(dialect); }

}  
