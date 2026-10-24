#include "md/block_parser.hpp"

#include <algorithm>
#include <string>

#include "lumos/log.hpp"
#include "md/link_syntax.hpp"
#include "md/ref_index.hpp"
#include "md/scanners.hpp"

namespace lumos::md {

namespace {

constexpr bool is_space_or_tab(char c) noexcept { return c == ' ' || c == '\t'; }

bool can_contain(NodeKind parent, NodeKind child) noexcept {
  return parent == NodeKind::document || parent == NodeKind::block_quote || parent == NodeKind::list_item ||
         (parent == NodeKind::list && child == NodeKind::list_item);
}

bool accepts_lines(NodeKind kind) noexcept {
  return kind == NodeKind::paragraph || kind == NodeKind::heading || kind == NodeKind::code_block ||
         kind == NodeKind::table;
}

bool has_non_space(std::string_view s) noexcept {
  for (char c : s)
    if (!is_space_or_tab(c)) return true;
  return false;
}

bool blank_sets_own_llb(NodeKind kind, bool fenced, bool item_opened_empty) noexcept {
  if (kind == NodeKind::block_quote || kind == NodeKind::heading || kind == NodeKind::thematic_break) return false;
  if (kind == NodeKind::code_block && fenced) return false;
  if (kind == NodeKind::list_item && item_opened_empty) return false;
  return true;
}

void log_decision(std::string_view event, std::size_t line, std::string_view key = {}, std::string_view value = {}) {
  if (!log_enabled(LogLevel::debug)) return;
  const std::string l = std::to_string(line);
  if (key.empty()) {
    log(LogLevel::debug, "parser", event, {{"line", l}});
  } else {
    log(LogLevel::debug, "parser", event, {{"line", l}, {key, value}});
  }
}

}  

void BlockOutput::clear() {
  nodes.clear();
  line_starts.clear();
  line_states.clear();
  definitions.clear();
  leaves.clear();
}

void BlockParser::begin(std::size_t text_size, BlockOutput& out, const TextSource& text, std::size_t start_line) {
  out_ = &out;
  text_ = &text;
  out.clear();
  text_size_ = text_size;
  line_no_ = start_line;
  state_ = document_state;
  open_.clear();
  recs_.clear();
  para_.clear();
  pending_.clear();
  taken_ranges_.clear();
  Node doc;
  doc.kind = NodeKind::document;
  doc.start = 0;
  doc.end = text_size;
  out.nodes.push_back(doc);
  recs_.push_back(Rec{});
  Open d;
  d.entry = document_state;
  d.kind = NodeKind::document;
  d.rec = 0;
  d.node = 0;
  open_.push_back(d);
}

void BlockParser::begin_run(std::size_t text_size, BlockOutput& out, std::size_t start_line,
                            const TextSource& text) {
  out_ = &out;
  text_ = &text;
  out.clear();
  text_size_ = text_size;
  line_no_ = start_line;
  state_ = document_state;
  open_.clear();
  recs_.clear();
  para_.clear();
  pending_.clear();
  taken_ranges_.clear();

  recs_.push_back(Rec{});  
  Open d;
  d.entry = document_state;
  d.kind = NodeKind::document;
  d.rec = 0;
  d.node = no_node;  
  open_.push_back(d);
}

std::uint32_t BlockParser::new_rec(NodeKind kind, std::size_t parent_index) {
  const auto id = static_cast<std::uint32_t>(recs_.size());
  Rec r;
  r.kind = kind;
  r.start_line = line_no_;
  r.parent = open_[parent_index].rec;
  r.prev = recs_[r.parent].last_child;
  Rec& p = recs_[r.parent];
  if (p.last_child == no_index) {
    p.first_child = id;
  } else {
    recs_[p.last_child].next = id;
  }
  p.last_child = id;
  recs_.push_back(r);
  return id;
}

void BlockParser::llb_write(std::uint32_t rec, bool value) {
  recs_[rec].llb = value;
  recs_[rec].llb_time = line_no_;
  if (!recs_[rec].open) set_last_line_blank(recs_[rec].node, value);
}

bool BlockParser::ends_with_blank_line(std::uint32_t rec) {
  std::uint32_t r = rec;
  while (!recs_[r].checked) {
    recs_[r].checked = true;
    if (recs_[r].kind != NodeKind::list && recs_[r].kind != NodeKind::list_item) break;
    if (recs_[r].last_child == no_index) break;
    r = recs_[r].last_child;
  }
  return recs_[r].llb;
}

std::uint32_t BlockParser::add_node(NodeKind kind, std::size_t start, std::size_t end, std::uint32_t parent) {
  Node n;
  n.kind = kind;
  n.start = start;
  n.end = end;
  n.parent = parent;
  out_->nodes.push_back(n);
  return static_cast<std::uint32_t>(out_->nodes.size() - 1);
}

void BlockParser::add_prefix_node(std::size_t start, std::size_t end, std::uint8_t virtual_spaces) {
  if (end <= start) return;
  const std::uint32_t i = add_node(NodeKind::block_continuation, start, end, deferred_parent);
  out_->nodes[i].virtual_spaces = virtual_spaces;
}

void BlockParser::set_end(std::uint32_t node_ref, std::size_t end) {
  if (node_ref == no_index) return;
  out_->nodes[node_ref].end = end;
}

void BlockParser::set_last_line_blank(std::uint32_t node_ref, bool value) {
  if (node_ref == no_index) return;
  out_->nodes[node_ref].last_line_blank = value;
}

BlockParser::Open& BlockParser::push(NodeKind kind, const OpenBlock& block, std::size_t parent, std::size_t start) {
  Open o;
  o.kind = kind;
  o.entry = arena_->push(open_[parent].entry, block);
  o.rec = new_rec(kind, parent);
  o.start = start;
  o.content_end = 0;  
  open_.push_back(o);
  state_ = o.entry;
  return open_.back();
}

void BlockParser::set_item_has_child(std::size_t index) {
  Open& o = open_[index];
  if (o.kind != NodeKind::list_item) return;
  const OpenBlock& b = arena_->get(o.entry);
  if ((b.small & item_empty) == 0) return;
  o.entry = arena_->with_flags(o.entry, static_cast<std::uint8_t>(b.small & ~item_empty));
  if (index + 1 == open_.size()) state_ = o.entry;
}

void BlockParser::set_item_empty(std::size_t index) {
  Open& o = open_[index];
  if (o.kind != NodeKind::list_item) return;
  const OpenBlock& b = arena_->get(o.entry);
  if (b.small & item_empty) return;
  o.entry = arena_->with_flags(o.entry, static_cast<std::uint8_t>(b.small | item_empty));
  if (index + 1 == open_.size()) state_ = o.entry;
}

void BlockParser::unlink_rec(std::uint32_t rec) {
  Rec& r = recs_[rec];
  Rec& p = recs_[r.parent];
  p.last_child = r.prev;
  if (r.prev == no_index) {
    p.first_child = no_index;
  } else {
    recs_[r.prev].next = no_index;
  }
}

void BlockParser::take(std::size_t depth, std::span<const Node> nodes, NodeId first_id, NodeId old_c,
                       std::ptrdiff_t shift, std::span<const TakenSibling> siblings,
                       std::span<const TakenLine> lines, std::size_t content_end_new, StateId next_line_state) {
  close_to(depth);
  Open& c = open_[depth];
  const auto base = static_cast<std::uint32_t>(out_->nodes.size());
  out_->nodes.insert(out_->nodes.end(), nodes.begin(), nodes.end());
  for (std::size_t k = base; k < out_->nodes.size(); ++k) {
    Node& n = out_->nodes[k];
    n.parent = n.parent == old_c ? c.node : base + (n.parent - first_id);
    n.start = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(n.start) + shift);
    n.end = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(n.end) + shift);
  }
  taken_ranges_.emplace_back(base, out_->nodes.size());
  for (const TakenSibling& s : siblings) {
    const std::uint32_t rec = new_rec(out_->nodes[base + s.node_index].kind, depth);
    Rec& r = recs_[rec];
    r.open = false;
    r.checked = true;
    r.llb = s.last_line_blank;
    r.node = base + static_cast<std::uint32_t>(s.node_index);
  }
  c.content_end = std::max(c.content_end, content_end_new);
  set_item_has_child(depth);
  out_->line_starts.reserve(out_->line_starts.size() + lines.size());
  out_->line_states.reserve(out_->line_states.size() + lines.size());
  for (const TakenLine& l : lines) {
    out_->line_starts.push_back(l.start);
    out_->line_states.push_back(l.state);
  }
  line_no_ += lines.size();
  force_next_line_state_ = true;
  forced_next_line_state_ = next_line_state;
}

std::size_t BlockParser::attach(NodeKind kind) {
  std::size_t p = open_.size() - 1;
  if (!opened_) {
    p = std::min(p, matched_index_);
    while (!can_contain(open_[p].kind, kind)) --p;
    if (p < matched_index_ && matched_index_ + 1 < open_.size()) close_matched_lists_first(p);
    close_to(p);
    add_prefix_node(line_start_, line_start_ + matched_pos_);
    opened_ = true;
  }
  set_item_has_child(p);
  return p;
}

void BlockParser::close_matched_lists_first(std::size_t p) {
  std::size_t cover = 0;
  for (std::size_t i = open_.size() - 1; i > p; --i) {
    cover = std::max(cover, open_[i].cover);
    Rec& r = recs_[open_[i].rec];
    if (cover > r.llb_time) r.llb = false;
  }
  for (std::size_t i = matched_index_; i > p; --i) {
    if (open_[i].kind != NodeKind::list) continue;
    close_list(open_[i]);
    open_[i].tight_done = true;
  }
}

void BlockParser::close_to(std::size_t index) {
  while (open_.size() - 1 > index) close_top();
}

BlockParser::EndLlb BlockParser::effective_close(std::size_t open_index) const {
  const Open& o = open_[open_index];
  const Rec& r = recs_[o.rec];
  return {o.content_end, o.cover > r.llb_time ? false : r.llb};
}

void BlockParser::close_top() {
  Open& o = open_.back();
  switch (o.kind) {
    case NodeKind::paragraph: close_paragraph(o); break;
    case NodeKind::code_block:
      if (!o.fenced) close_indented_code(o);
      break;
    case NodeKind::list: close_list(o); break;
    default: break;
  }
  const EndLlb end_llb = effective_close(open_.size() - 1);
  set_end(o.node, end_llb.end);
  Rec& r = recs_[o.rec];
  r.llb = end_llb.last_line_blank;  
  r.open = false;
  r.node = o.node;
  set_last_line_blank(o.node, end_llb.last_line_blank);
  Open& parent = open_[open_.size() - 2];
  parent.cover = std::max(parent.cover, o.cover);
  parent.content_end = std::max(parent.content_end, o.content_end);
  open_.pop_back();
  state_ = open_.back().entry;
}

void BlockParser::emit_paragraph_lines(std::uint32_t parent, bool strip_pipe_escapes) {
  std::size_t last = para_.size();
  for (std::size_t i = para_.size(); i-- > 0;) {
    if (para_[i].trimmed_end > para_[i].content_start) {
      last = i;
      break;
    }
  }
  BlockOutput::Leaf leaf;
  leaf.node = parent;
  for (std::size_t i = 0; i < para_.size(); ++i) {
    const ParaLine& l = para_[i];
    if (i > 0) add_prefix_node(l.prefix_start, l.prefix_end);
    if (last == para_.size() || i > last) continue;
    const std::size_t end = i == last ? l.trimmed_end : l.line_end;
    if (end > l.content_start) {
      if (strip_pipe_escapes) {
        append_pipe_escaped(l.content_start, end, leaf.segments);
      } else {
        leaf.segments.push_back({l.content_start, end});
      }
    }
  }
  if (!leaf.segments.empty()) out_->leaves.push_back(std::move(leaf));
  para_.clear();
}

void BlockParser::append_pipe_escaped(std::size_t start, std::size_t end, std::vector<Segment>& segments) {
  if (end <= start) return;
  const std::string bytes = text_->slice(start, end - start);
  std::size_t seg_start = start;
  std::size_t i = start;
  while (i + 1 < end) {
    if (bytes[i - start] == '\\' && bytes[i + 1 - start] == '|') {
      if (i > seg_start) segments.push_back({seg_start, i});
      add_escape_node(i);
      seg_start = i + 1;
      i += 2;
    } else {
      ++i;
    }
  }
  if (end > seg_start) segments.push_back({seg_start, end});
}

void BlockParser::add_table_row_nodes(const TableRowScan& row, std::size_t base, std::uint32_t row_node,
                                      bool is_delimiter, std::span<const Alignment> aligns) {
  std::size_t col = 0;
  for (const TableToken& t : row.tokens) {
    if (t.pipe) {
      add_node(NodeKind::table_pipe, base + t.start, base + t.end, row_node);
      continue;
    }
    if (t.end > t.start) {
      const NodeKind kind = is_delimiter ? NodeKind::table_delimiter_cell : NodeKind::table_cell;
      const std::uint32_t cell = add_node(kind, base + t.start, base + t.end, row_node);
      if (col < aligns.size()) out_->nodes[cell].align = aligns[col];
      if (!is_delimiter) {
        BlockOutput::Leaf leaf;
        leaf.node = cell;
        append_pipe_escaped(base + t.start, base + t.end, leaf.segments);
        if (!leaf.segments.empty()) out_->leaves.push_back(std::move(leaf));
      }
    }
    ++col;
  }
}

bool BlockParser::try_start_table(std::size_t index, const Line& line, std::size_t fns, std::string_view rest) {
  if (para_.empty()) return false;  
  const auto delim = scan_table_delimiter_row(rest);
  if (!delim) return false;
  Open& o = open_[index];
  const ParaLine last_line = para_.back();
  const std::string header_bytes = text_->slice(last_line.row_start, last_line.text_end - last_line.row_start);
  auto header = scan_table_row(header_bytes);
  for (std::size_t i = 0; header && i + 1 < para_.size(); ++i) {
    const ParaLine& l = para_[i];
    if (!is_valid_utf8(text_->slice(l.content_start, l.text_end - l.content_start))) header.reset();
  }
  if (!header || header->cells != delim->row.cells) {
    o.table_tried = true;
    log_decision("table_tried", line_no_);
    return false;
  }
  const std::size_t header_count = header->cells;
  log_decision("table_start", line_no_, "columns", std::to_string(header_count));
  table_started_ = true;

  const std::size_t parent_index = index - 1;
  const std::uint32_t parent_node = open_[parent_index].node;

  const bool had_earlier_lines = para_.size() > 1;
  if (had_earlier_lines) {
    const ParaLine first_line = para_.front();
    const ParaLine before_last = para_[para_.size() - 2];
    const std::uint32_t pnode = add_node(NodeKind::paragraph, first_line.content_start, before_last.line_end,
                                         parent_node);
    para_.pop_back();
    emit_paragraph_lines(pnode,  true);
  } else {
    para_.clear();
  }

  if (had_earlier_lines) add_prefix_node(last_line.prefix_start, std::min(last_line.prefix_end, last_line.row_start));

  const std::size_t table_start = last_line.row_start;
  o.node = add_node(NodeKind::table, table_start, line.end(), parent_node);
  OpenBlock b;
  b.kind = OpenKind::table;
  o.entry = arena_->push(open_[parent_index].entry, b);
  if (index + 1 == open_.size()) state_ = o.entry;
  o.kind = NodeKind::table;
  o.start = table_start;
  o.content_end = line.end();
  recs_[o.rec].kind = NodeKind::table;
  o.table_align = std::move(delim->align);
  o.table_rows = 1;  
  o.table_filled_cells = header_count;

  const std::uint32_t header_row = add_node(NodeKind::table_header_row, table_start, last_line.line_end, o.node);
  add_table_row_nodes(*header, last_line.row_start, header_row,  false, o.table_align);

  add_prefix_node(line.start, line.start + fns);
  const std::size_t delim_base = line.start + fns;
  const std::uint32_t delim_row = add_node(NodeKind::table_delimiter_row, delim_base, line.end(), o.node);
  add_table_row_nodes(delim->row, delim_base, delim_row,  true, o.table_align);
  return true;
}

bool BlockParser::try_add_table_row(std::size_t index, const Line& line, std::size_t fns, std::string_view rest) {
  const auto row = scan_table_row(rest);
  if (!row) return false;
  Open& o = open_[index];
  const std::size_t header_count = o.table_align.size();
  const std::size_t filled = std::min(row->cells, header_count);
  if (header_count * (o.table_rows + 1) - (o.table_filled_cells + filled) > 524288) {
    log_decision("table_cap", line_no_);
    return false;
  }
  o.table_rows += 1;
  o.table_filled_cells += filled;
  add_prefix_node(line.start, line.start + fns);
  const std::size_t base = line.start + fns;
  const std::uint32_t row_node = add_node(NodeKind::table_row, base, line.end(), o.node);
  add_table_row_nodes(*row, base, row_node,  false, o.table_align);
  o.content_end = line.end();
  return true;
}

bool BlockParser::take_paragraph_definitions(std::size_t index, bool keep_open) {
  Open& o0 = open_[index];
  if (para_.empty()) {
    if (!keep_open) {
      const std::size_t parent0 = index - 1;
      unlink_rec(o0.rec);
      if (recs_[open_[parent0].rec].first_child == no_index) set_item_empty(parent0);
    }
    return false;
  }
  const ParaLine& head = para_.front();
  const std::string_view head_chunk = text_->chunk_at(head.content_start);
  if (head_chunk.empty() || head_chunk[0] != '[') return true;

  ContentBuffer buf;
  std::vector<std::size_t> line_end;  
  line_end.reserve(para_.size());
  std::size_t acc = 0;
  for (const ParaLine& l : para_) {
    const std::size_t len = l.line_end - l.content_start;
    buf.append(l.content_start, text_->slice(l.content_start, len));
    acc += len;
    line_end.push_back(acc);
  }
  const std::string_view text = buf.text();

  struct Taken {
    DefinitionSyntax def;
    std::size_t lines;  
  };
  std::vector<Taken> taken;
  std::size_t offset = 0, line_idx = 0;
  while (offset < text.size() && text[offset] == '[') {
    const auto found = scan_link_reference_definition(text.substr(offset));
    if (!found) break;
    DefinitionSyntax def = *found;
    def.label_start += offset;
    def.label_end += offset;
    def.colon += offset;
    def.destination.start += offset;
    def.destination.end += offset;
    def.title_start += offset;
    def.title_end += offset;
    def.end += offset;
    while (line_idx < line_end.size() && line_end[line_idx] < def.end) ++line_idx;
    ++line_idx;
    taken.push_back({def, line_idx});
    offset = def.end;
  }
  if (taken.empty()) return true;

  Open& o = open_[index];
  const std::size_t parent = index - 1;
  const std::uint32_t parent_node = open_[parent].node;

  auto line_of = [&](std::size_t buf_pos) {
    return static_cast<std::size_t>(std::upper_bound(line_end.begin(), line_end.end(), buf_pos) - line_end.begin());
  };
  std::size_t prev_lines = 0;  
  auto ensure_prefix = [&](std::size_t buf_pos) {
    const std::size_t li = line_of(buf_pos);
    for (std::size_t i = std::max<std::size_t>(prev_lines, 1); i <= li && i < para_.size(); ++i) {
      add_prefix_node(para_[i].prefix_start, para_[i].prefix_end);
    }
    prev_lines = std::max(prev_lines, li + 1);
  };
  for (const Taken& t : taken) {
    ensure_prefix(t.def.label_start);
    const std::size_t def_start = buf.source_start(t.def.label_start);
    const std::size_t def_end = buf.source_end(t.def.end);
    const std::uint32_t node = add_node(NodeKind::link_reference_definition, def_start, def_end, parent_node);
    add_node(NodeKind::link_label, buf.source_start(t.def.label_start), buf.source_end(t.def.label_end), node);
    ensure_prefix(t.def.colon);
    add_node(NodeKind::link_delimiter, buf.source_start(t.def.colon), buf.source_end(t.def.colon + 1), node);
    const Destination& dst = t.def.destination;
    ensure_prefix(dst.start);
    if (dst.pointy) {
      add_node(NodeKind::link_delimiter, buf.source_start(dst.start), buf.source_end(dst.start + 1), node);
      add_node(NodeKind::link_destination, buf.source_start(dst.start + 1), buf.source_end(dst.end - 1), node);
      add_node(NodeKind::link_delimiter, buf.source_start(dst.end - 1), buf.source_end(dst.end), node);
    } else {
      add_node(NodeKind::link_destination, buf.source_start(dst.start), buf.source_end(dst.end), node);
    }
    if (t.def.title_start != t.def.title_end) {
      ensure_prefix(t.def.title_start);
      add_node(NodeKind::link_title, buf.source_start(t.def.title_start), buf.source_end(t.def.title_end), node);
    }
    prev_lines = std::max(prev_lines, t.lines);
    const std::size_t label_len = t.def.label_end - t.def.label_start - 2;
    const std::string_view raw_label = text.substr(t.def.label_start + 1, label_len);
    out_->definitions.push_back({def_start, normalize_label(raw_label)});
  }

  if (log_enabled(LogLevel::debug)) {
    log(LogLevel::debug, "parser", "definitions",
        {{"line", std::to_string(line_no_)}, {"count", std::to_string(taken.size())},
         {"rest", prev_lines < para_.size() ? "1" : "0"}});
  }

  const std::size_t consumed = prev_lines;
  para_.erase(para_.begin(), para_.begin() + static_cast<std::ptrdiff_t>(consumed));
  const bool remains = !para_.empty();

  if (remains) {
    o.start = para_.front().content_start;
    add_prefix_node(para_.front().prefix_start, para_.front().prefix_end);
  } else if (!keep_open) {
    unlink_rec(o.rec);
    if (recs_[open_[parent].rec].first_child == no_index) set_item_empty(parent);
  }
  return remains;
}

void BlockParser::close_paragraph(Open& o) {
  const std::size_t index = open_.size() - 1;
  if (!take_paragraph_definitions(index,  false)) return;  
  o.node = add_node(NodeKind::paragraph, o.start, o.content_end, open_[index - 1].node);
  emit_paragraph_lines(o.node);
}

void BlockParser::close_indented_code(Open& o) {
  for (const PendingBlank& b : pending_)
    if (b.marker) add_prefix_node(b.prefix_start, b.prefix_end);
  pending_.clear();
  set_end(o.content, o.content_end);
}

void BlockParser::close_list(Open& o) {
  if (o.tight_done) return;  
  bool tight = true;
  const char* reason = "";
  for (std::uint32_t item = recs_[o.rec].first_child; item != no_index && tight; item = recs_[item].next) {
    if (recs_[item].llb && recs_[item].next != no_index) {
      tight = false;
      reason = "item_blank";
      break;
    }
    for (std::uint32_t sub = recs_[item].first_child; sub != no_index; sub = recs_[sub].next) {
      if ((recs_[item].next != no_index || recs_[sub].next != no_index) && ends_with_blank_line(sub)) {
        tight = false;
        reason = "child_blank";
        break;
      }
    }
  }
  out_->nodes[o.node].tight = tight;
  if (!tight) log_decision("list_loose", line_no_, "reason", reason);
}

void BlockParser::add_para_line(const Line& line, std::size_t prefix_end, std::size_t content_start,
                                std::size_t row_start) {
  ParaLine l;
  l.prefix_start = line.start;
  l.prefix_end = line.start + prefix_end;
  l.content_start = line.start + content_start;
  l.line_end = line.end();
  l.row_start = line.start + row_start;
  l.text_end = line.content_end();
  const std::string_view t = line.text;
  std::size_t e = t.size();
  while (e > content_start && is_cmark_space(t[e - 1])) --e;
  l.trimmed_end = line.start + e;
  if (para_.empty() && open_.back().content_end != 0) {
    open_.back().start = l.content_start;
    add_prefix_node(l.prefix_start, l.prefix_end);
  }
  para_.push_back(l);
  open_.back().content_end = line.end();
}

void BlockParser::add_literal_line(Open& o, const Line& line, LineCursor& cur) {
  const std::size_t content = line.start + cur.content_pos();
  const auto vs = static_cast<std::uint8_t>(cur.partial_rest());
  if (o.kind == NodeKind::code_block && !o.fenced && cur.blank()) {
    PendingBlank b;
    b.prefix_start = line.start;
    b.prefix_end = content;
    b.virtual_spaces = vs;
    b.marker = has_non_space(line.text.substr(0, cur.content_pos()));
    pending_.push_back(b);
    return;
  }
  for (const PendingBlank& b : pending_) add_prefix_node(b.prefix_start, b.prefix_end, b.virtual_spaces);
  pending_.clear();
  if (o.kind == NodeKind::code_block && o.fenced && o.content == no_index) {
    o.content = add_node(NodeKind::code_content, line.start, line.end(), o.node);
  }
  add_prefix_node(line.start, content, vs);
  o.content_end = line.end();
  set_end(o.content, line.end());
}

void BlockParser::convert_to_setext(std::size_t index, std::uint8_t level, std::size_t marker_start,
                                    std::size_t marker_end, const Line& line) {
  Open& o = open_[index];
  o.node = add_node(NodeKind::heading, o.start, line.end(), open_[index - 1].node);
  Node& h = out_->nodes[o.node];
  h.level = level;
  h.setext = true;
  emit_paragraph_lines(o.node);
  add_prefix_node(line.start, line.start + marker_start);
  add_node(NodeKind::heading_marker, line.start + marker_start, line.start + marker_end, o.node);
  o.kind = NodeKind::heading;
  o.content_end = line.end();
  recs_[o.rec].kind = NodeKind::heading;
  log_decision("setext_heading", line_no_);
}

void BlockParser::set_list_blank(std::size_t index, bool value) {
  if (open_[index].list_blank == value) return;
  open_[index].list_blank = value;
  flags_changed_from_ = std::min(flags_changed_from_, index);
}

void BlockParser::reintern_from(std::size_t index) {
  for (std::size_t i = index; i < open_.size(); ++i) {
    Open& o = open_[i];
    if (o.kind == NodeKind::heading || o.kind == NodeKind::thematic_break) {
      o.entry = open_[i - 1].entry;
      continue;
    }
    OpenBlock b = arena_->get(o.entry);
    b.parent = open_[i - 1].entry;
    if (o.kind == NodeKind::list) b.small = o.list_blank ? list_blank : 0;
    o.entry = arena_->intern(b);
  }
  state_ = open_.back().entry;
}

std::size_t BlockParser::match_open_blocks(LineCursor& cur, const Line& line, bool& all_matched,
                                           bool& fence_closed, bool& early_exit) {
  std::size_t k = 1;
  std::size_t last_quote = 0;
  while (k < open_.size()) {
    Open& o = open_[k];
    switch (o.kind) {
      case NodeKind::block_quote: {
        const std::size_t ind = cur.indent();
        if (ind > 3 || cur.at(cur.first_nonspace()) != '>') goto done;
        cur.advance(ind + 1, true);
        if (is_space_or_tab(cur.peek())) cur.advance(1, true);
        last_quote = k;
        break;
      }
      case NodeKind::list:
        if (cur.blank()) {
          if (o.list_blank && cur.indent() == 0) {
            if (last_quote) open_[last_quote].content_end = line.end();
            early_exit = true;
            all_matched = false;
            return k;
          }
          set_list_blank(k, true);
        } else {
          set_list_blank(k, false);
        }
        break;
      case NodeKind::list_item: {
        const OpenBlock& b = arena_->get(o.entry);
        if (cur.indent() >= b.width) {
          cur.advance(b.width, true);
        } else if (cur.blank() && (b.small & item_empty) == 0) {
          cur.skip_to_first_nonspace();
        } else {
          goto done;
        }
        break;
      }
      case NodeKind::code_block:
        if (o.fenced) {
          const std::size_t fns = cur.first_nonspace();
          if (cur.indent() <= 3 && cur.at(fns) == o.fence_char) {
            const std::size_t n = scan_fence_close(line.text.substr(fns));
            if (n >= o.fence_length) {
              if (last_quote) open_[last_quote].content_end = line.end();
              add_prefix_node(line.start, line.start + fns);
              add_node(NodeKind::code_fence_delimiter, line.start + fns, line.start + fns + n, o.node);
              o.content_end = line.end();
              fence_closed = true;
              all_matched = true;
              return k;
            }
          }
          for (std::size_t i = o.fence_offset; i > 0 && is_space_or_tab(cur.peek()); --i) cur.advance(1, true);
        } else {
          if (cur.indent() >= 4) {
            cur.advance(4, true);
          } else if (cur.blank()) {
            cur.skip_to_first_nonspace();
          } else {
            goto done;
          }
        }
        break;
      case NodeKind::html_block:
        if (o.html_type >= 6 && cur.blank()) goto done;
        break;
      case NodeKind::paragraph:
        if (cur.blank()) goto done;
        break;
      case NodeKind::table: {
        const std::size_t fns = cur.first_nonspace();
        if (!is_table_row(line.text.substr(std::min(fns, line.text.size())))) goto done;
        break;
      }
      default: goto done;
    }
    ++k;
  }
done:
  if (last_quote) open_[last_quote].content_end = line.end();
  all_matched = k == open_.size();
  return k - 1;
}

void BlockParser::open_new_blocks(LineCursor& cur, const Line& line, std::size_t last_matched, bool all_matched) {
  const std::size_t ls = line.start;
  const std::string_view text = line.text;
  bool maybe_lazy = open_.back().kind == NodeKind::paragraph;
  NodeKind ck = open_[last_matched].kind;
  std::size_t container = last_matched;
  while (ck != NodeKind::code_block && ck != NodeKind::html_block) {
    const std::size_t fns = cur.first_nonspace();
    const std::size_t ind = cur.indent();
    const bool indented = ind >= 4;
    const char c = cur.at(fns);
    const std::string_view rest = text.substr(std::min(fns, text.size()));
    std::size_t cs = 0;
    std::size_t run_end = 0;
    std::uint8_t level = 0;
    std::uint8_t html = 0;
    std::size_t fence = 0;
    std::optional<ListMarker> marker;
    NodeKind added = NodeKind::document;

    if (!indented && c == '>') {
      const std::size_t p = attach(NodeKind::block_quote);
      cur.advance(fns + 1 - cur.pos(), false);
      if (is_space_or_tab(cur.peek())) cur.advance(1, true);
      OpenBlock b;
      b.kind = OpenKind::block_quote;
      Open& o = push(NodeKind::block_quote, b, p, ls + fns);
      o.node = add_node(NodeKind::block_quote, ls + fns, line.end(), open_[p].node);
      o.content_end = line.end();
      add_node(NodeKind::block_quote_marker, ls + fns, ls + fns + 1, o.node);
      added = NodeKind::block_quote;
    } else if (!indented && (level = scan_atx_heading_start(rest, &cs)) != 0) {
      const std::size_t p = attach(NodeKind::heading);
      cur.advance(fns + cs - cur.pos(), false);
      Open o;
      o.kind = NodeKind::heading;
      o.entry = open_[p].entry;  
      o.rec = new_rec(NodeKind::heading, p);
      o.start = ls + fns;
      o.content_end = line.end();
      o.node = add_node(NodeKind::heading, ls + fns, line.end(), open_[p].node);
      out_->nodes[o.node].level = level;
      add_node(NodeKind::heading_marker, ls + fns, ls + fns + level, o.node);
      open_.push_back(o);
      added = NodeKind::heading;
    } else if (!indented && (fence = scan_fence_open(rest)) != 0) {
      const std::size_t p = attach(NodeKind::code_block);
      OpenBlock b;
      b.kind = OpenKind::fenced_code;
      b.marker = c;
      b.width = static_cast<std::uint32_t>(std::min<std::size_t>(fence, 255));
      b.small = static_cast<std::uint8_t>(fns - cur.pos());
      Open& o = push(NodeKind::code_block, b, p, ls + fns);
      o.fenced = true;
      o.fence_char = c;
      o.fence_length = b.width;
      o.fence_offset = b.small;
      o.node = add_node(NodeKind::code_block, ls + fns, line.end(), open_[p].node);
      o.content_end = line.end();
      out_->nodes[o.node].fenced = true;
      add_node(NodeKind::code_fence_delimiter, ls + fns, ls + fns + fence, o.node);
      cur.advance(fns + fence - cur.pos(), false);
      added = NodeKind::code_block;
    } else if (!indented &&
               ((html = scan_html_block_start(rest)) != 0 ||
                (ck != NodeKind::paragraph && !maybe_lazy && (html = scan_html_block_start_7(rest)) != 0))) {
      const std::size_t p = attach(NodeKind::html_block);
      OpenBlock b;
      b.kind = OpenKind::html_block;
      b.small = html;
      const std::size_t start = ls + cur.content_pos();
      Open& o = push(NodeKind::html_block, b, p, start);
      o.html_type = html;
      o.node = add_node(NodeKind::html_block, start, line.end(), open_[p].node);
      o.content = o.node;
      o.content_end = line.end();
      out_->nodes[o.node].variant = html;
      out_->nodes[o.node].virtual_spaces = static_cast<std::uint8_t>(cur.partial_rest());
      added = NodeKind::html_block;
    } else if (!indented && ck == NodeKind::paragraph && (level = scan_setext_underline(rest, &run_end)) != 0 &&
               [&] {
                 if (take_paragraph_definitions(container,  true)) return true;
                 log_decision("setext_rejected", line_no_, "reason", "only_definitions");
                 return false;
               }()) {
      convert_to_setext(container, level, fns, fns + run_end, line);
      setext_ = true;
      cur.advance(text.size() - cur.pos(), false);
      added = NodeKind::heading;
    } else if (!indented && !(ck == NodeKind::paragraph && !all_matched) && thematic_fail_ <= fns &&
               [&] {
                 std::size_t fail = 0;
                 if (scan_thematic_break(rest, &fail)) return true;
                 thematic_fail_ = fns + fail;
                 return false;
               }()) {
      const std::size_t p = attach(NodeKind::thematic_break);
      Open o;
      o.kind = NodeKind::thematic_break;
      o.entry = open_[p].entry;
      o.rec = new_rec(NodeKind::thematic_break, p);
      o.start = ls + fns;
      o.content_end = line.end();
      o.node = add_node(NodeKind::thematic_break, ls + fns, line.end(), open_[p].node);
      open_.push_back(o);
      cur.advance(text.size() - cur.pos(), false);
      added = NodeKind::thematic_break;
    } else if (ind < 4 && (marker = scan_list_marker(rest, ck == NodeKind::paragraph))) {
      cur.advance(fns + marker->width - cur.pos(), false);
      const LineCursor::Mark saved = cur.mark();
      const std::size_t save_col = cur.column();
      while (cur.column() - save_col <= 5 && is_space_or_tab(cur.peek())) cur.advance(1, true);
      const std::size_t spaces = cur.column() - save_col;
      std::size_t padding = 0;
      if (spaces >= 5 || spaces < 1 || cur.at_end()) {
        padding = marker->width + 1;
        cur.reset(saved);
        if (spaces > 0) cur.advance(1, true);
      } else {
        padding = marker->width + spaces;
      }
      const auto width = static_cast<std::uint32_t>(ind + padding);
      const bool same_list = ck == NodeKind::list && arena_->get(open_[container].entry).marker == marker->ch;
      std::size_t p = attach(same_list ? NodeKind::list_item : NodeKind::list);
      if (!same_list) {
        OpenBlock lb;
        lb.kind = OpenKind::list;
        lb.marker = marker->ch;
        Open& l = push(NodeKind::list, lb, p, ls + fns);
        l.node = add_node(NodeKind::list, ls + fns, line.end(), open_[p].node);
        l.content_end = line.end();
        out_->nodes[l.node].ordered = marker->ordered;
        out_->nodes[l.node].variant = static_cast<std::uint8_t>(marker->ch);
        p = open_.size() - 1;
      }
      OpenBlock ib;
      ib.kind = OpenKind::list_item;
      ib.marker = marker->ch;
      ib.width = width;
      ib.small = item_empty;
      Open& it = push(NodeKind::list_item, ib, p, ls + fns);
      it.node = add_node(NodeKind::list_item, ls + fns, line.end(), open_[p].node);
      it.content_end = line.end();
      add_node(NodeKind::list_marker, ls + fns, ls + fns + marker->width, it.node);
      added = NodeKind::list_item;
      if (features_.task_lists && !(line_no_ == 1 && ls != 0)) {
        if (const auto task = scan_task_marker(text, fns, cur.pos())) {
          const std::uint32_t tnode = add_node(NodeKind::task_marker, ls + task->start, ls + task->end, it.node);
          out_->nodes[tnode].checked = task->checked;
          cur.advance(task->end - cur.pos(), false);
          return;
        }
      }
    } else if (indented && !maybe_lazy && !cur.blank()) {
      cur.advance(4, true);
      const std::size_t p = attach(NodeKind::code_block);
      OpenBlock b;
      b.kind = OpenKind::indented_code;
      const std::size_t start = ls + cur.content_pos();
      Open& o = push(NodeKind::code_block, b, p, start);
      o.node = add_node(NodeKind::code_block, start, line.end(), open_[p].node);
      o.content = add_node(NodeKind::code_content, start, line.end(), o.node);
      o.content_end = line.end();
      out_->nodes[o.content].virtual_spaces = static_cast<std::uint8_t>(cur.partial_rest());
      added = NodeKind::code_block;
    } else if (!indented && features_.tables && ck == NodeKind::table &&
               try_add_table_row(container, line, fns, rest)) {
      added = NodeKind::table;
      table_line_ = true;
    } else if (!indented && features_.tables && ck == NodeKind::paragraph && !open_[container].table_tried &&
               try_start_table(container, line, fns, rest)) {
      added = NodeKind::table;
      table_line_ = true;
    } else {
      break;
    }
    container = open_.size() - 1;
    if (accepts_lines(added)) break;
    ck = added;
    maybe_lazy = false;
  }
}

void BlockParser::add_text(LineCursor& cur, const Line& line, std::size_t last_matched) {
  const std::size_t container = (opened_ || setext_ || table_line_) ? open_.size() - 1 : last_matched;
  const bool blank = cur.blank();
  Open& c = open_[container];
  if (blank && recs_[c.rec].last_child != no_index) llb_write(recs_[c.rec].last_child, true);
  const NodeKind ck = c.kind;
  const bool fenced = ck == NodeKind::code_block && c.fenced;
  const bool empty_new_item = ck == NodeKind::list_item && recs_[c.rec].first_child == no_index &&
                              recs_[c.rec].start_line == line_no_;
  const bool own_llb = table_started_ || (blank && blank_sets_own_llb(ck, fenced, empty_new_item));
  llb_write(c.rec, own_llb);
  if (container > 0) open_[container - 1].cover = line_no_;

  const bool lazy = !opened_ && !setext_ && !table_line_ && last_matched + 1 < open_.size() && !blank &&
                    open_.back().kind == NodeKind::paragraph;
  if (lazy) {
    log_decision("lazy_line", line_no_);
    const std::size_t fns = cur.first_nonspace();
    const std::size_t matched = cur.pos();
    cur.skip_to_first_nonspace();
    add_para_line(line, fns, fns, matched);
    return;
  }
  if (setext_) return;     
  if (table_line_) return;  
  if (!opened_) close_to(last_matched);
  Open& o = open_.back();
  switch (o.kind) {
    case NodeKind::code_block:
      if (o.fenced && recs_[o.rec].start_line == line_no_) {
        std::size_t a = cur.pos();
        std::size_t b = line.text.size();
        while (a < b && is_cmark_space(line.text[a])) ++a;
        while (b > a && is_cmark_space(line.text[b - 1])) --b;
        if (b > a) add_node(NodeKind::info_string, line.start + a, line.start + b, o.node);
      } else if (!o.fenced && recs_[o.rec].start_line == line_no_) {
      } else {
        add_literal_line(o, line, cur);
      }
      return;
    case NodeKind::html_block: {
      if (recs_[o.rec].start_line != line_no_) add_literal_line(o, line, cur);
      const std::size_t fns = cur.first_nonspace();
      if (html_block_ends_on_line(o.html_type, line.text.substr(std::min(fns, line.text.size())))) close_top();
      return;
    }
    default: break;
  }
  if (blank) {
    if (!opened_ && has_non_space(line.text.substr(0, matched_pos_)))
      add_prefix_node(line.start, line.start + matched_pos_);
    return;
  }
  if (o.kind == NodeKind::paragraph) {
    const std::size_t fns = cur.first_nonspace();
    cur.skip_to_first_nonspace();
    add_para_line(line, fns, fns, fns);
    return;
  }
  if (o.kind == NodeKind::heading) {
    const AtxContent ac = scan_atx_content(line.text, cur.pos());
    if (ac.content_end > cur.pos()) {
      BlockOutput::Leaf leaf;
      leaf.node = o.node;
      leaf.segments.push_back({line.start + cur.pos(), line.start + ac.content_end});
      out_->leaves.push_back(std::move(leaf));
    }
    if (ac.closing_end > ac.closing_start)
      add_node(NodeKind::heading_marker, line.start + ac.closing_start, line.start + ac.closing_end, o.node);
    return;
  }
  if (o.kind == NodeKind::thematic_break) return;
  const std::size_t fns = cur.first_nonspace();
  const std::size_t p = attach(NodeKind::paragraph);
  OpenBlock b;
  b.kind = OpenKind::paragraph;
  push(NodeKind::paragraph, b, p, line.start + fns);
  cur.skip_to_first_nonspace();
  add_para_line(line, fns, fns, fns);
}

StateId BlockParser::feed(const Line& line) {
  ++line_no_;
  line_start_ = line.start;
  out_->line_starts.push_back(line.start);
  out_->line_states.push_back(force_next_line_state_ ? forced_next_line_state_ : state_);
  force_next_line_state_ = false;
  opened_ = false;
  setext_ = false;
  table_line_ = false;
  table_started_ = false;
  thematic_fail_ = 0;
  LineCursor cur(line.text);
  bool all_matched = false;
  bool fence_closed = false;
  bool early_exit = false;
  flags_changed_from_ = SIZE_MAX;
  const std::size_t last_matched = match_open_blocks(cur, line, all_matched, fence_closed, early_exit);
  if (flags_changed_from_ != SIZE_MAX) reintern_from(flags_changed_from_);
  if (fence_closed) {
    close_top();
    return state_;
  }
  if (early_exit) {
    Open& top = open_.back();
    if (top.kind == NodeKind::code_block || top.kind == NodeKind::html_block) {
      add_literal_line(top, line, cur);
    } else if (has_non_space(line.text.substr(0, cur.pos()))) {
      add_prefix_node(line.start, line.start + cur.pos());
    }
    return state_;
  }
  matched_pos_ = cur.pos();
  matched_index_ = last_matched;
  open_new_blocks(cur, line, last_matched, all_matched);
  add_text(cur, line, last_matched);
  const NodeKind top = open_.back().kind;
  if (top == NodeKind::heading || top == NodeKind::thematic_break) close_top();
  return state_;
}

void BlockParser::resolve_prefixes(std::uint32_t root_ref, std::size_t start_index) {
  std::vector<Node>& nodes = out_->nodes;
  struct Frame {
    std::size_t start;
    std::size_t end;
    std::uint32_t ref;
  };
  std::vector<Frame> stack;
  stack.reserve(64);
  stack.push_back({0, SIZE_MAX, root_ref});
  auto taken = taken_ranges_.begin();
  for (std::size_t i = start_index; i < nodes.size(); ++i) {
    if (taken != taken_ranges_.end() && i == taken->first) {
      i = taken->second - 1;
      ++taken;
      continue;
    }
    Node& n = nodes[i];
    if (n.parent == deferred_parent) {
      while (stack.size() > 1 && !(stack.back().start <= n.start && n.end <= stack.back().end)) stack.pop_back();
      n.parent = stack.back().ref;
    } else {
      while (stack.size() > 1 && stack.back().ref != n.parent) stack.pop_back();
    }
    stack.push_back({n.start, n.end, static_cast<std::uint32_t>(i)});
  }
}

void BlockParser::finish() {
  close_to(0);
  out_->nodes[0].end = text_size_;
  resolve_prefixes(0, 1);
  if (log_enabled(LogLevel::debug))
    log(LogLevel::debug, "parser", "blocks_done",
        {{"lines", std::to_string(line_no_)}, {"nodes", std::to_string(out_->nodes.size())},
         {"tables", features_.tables ? "on" : "off"}});
}

void BlockParser::finish_run() {
  close_to(0);
  resolve_prefixes(no_node, 0);
}

}  
