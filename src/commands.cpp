#include "lumos/commands.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>

#include "commands_detail.hpp"
#include "lumos/log.hpp"

namespace lumos {

namespace detail {

void log_decision(std::string_view event, std::string_view kind, std::size_t edits) {
  if (log_enabled(LogLevel::debug)) {
    log(LogLevel::debug, "commands", event, {{"kind", kind}, {"edits", std::to_string(edits)}});
  }
}

LineText line_text(const Buffer& b, std::size_t line) {
  const std::size_t start = b.line_start(line);
  std::size_t end = line + 1 < b.line_count() ? b.line_start(line + 1) - 1 : b.size();
  if (end > start && b.at(end - 1) == '\r') --end;
  return {start, b.slice(start, end - start)};
}

std::optional<Context> make_context(const Document& doc, NodeId id) {
  const SyntaxTree& tree = doc.tree();
  const Node& n = tree[id];
  const LineText line = line_text(doc.buffer(), doc.buffer().line_of(n.start));
  const std::string& t = line.text;
  Context c;
  c.node = id;
  c.from = n.start - line.start;
  if (n.kind == NodeKind::block_quote) {
    if (c.from >= t.size() || t[c.from] != '>') return std::nullopt;
    c.quote = true;
    c.after = c.from + 1 < t.size() && t[c.from + 1] == ' ' ? " " : "";
    c.mark_end = c.from + 1;
    c.to = c.mark_end + c.after.size();
    return c;
  }
  NodeId marker = no_node;
  NodeId task = no_node;
  for (NodeId child : tree.children(id)) {
    if (tree[child].kind == NodeKind::list_marker) marker = child;
    if (tree[child].kind == NodeKind::task_marker && tree[child].end <= line.end()) task = child;
  }
  if (marker == no_node) return std::nullopt;
  const Node& list = tree[n.parent];
  c.list = n.parent;
  c.ordered = list.ordered;
  const std::size_t m_end = tree[marker].end - line.start;
  c.mark_end = m_end;
  const std::string mark = t.substr(c.from, m_end - c.from);
  if (c.ordered) {
    c.bullet = mark.substr(mark.size() - 1);
    const std::string digits = mark.substr(0, mark.size() - 1);
    for (char d : digits) c.number = c.number * 10 + static_cast<std::uint64_t>(d - '0');
  } else {
    c.bullet = mark;
  }
  std::size_t i = m_end;
  if (task != no_node) {
    const std::size_t t_end = tree[task].end - line.start;
    c.task = t.substr(i, t_end - i);
    std::replace_if(c.task.begin(), c.task.end(), [](char ch) { return ch == 'x' || ch == 'X'; }, ' ');
    i = t_end;
  }
  std::size_t spaces = 0;
  while (i + spaces < t.size() && t[i + spaces] == ' ') ++spaces;
  if (spaces >= 5) spaces = 1;
  c.after.assign(spaces, ' ');
  c.to = i + spaces;
  return c;
}

std::vector<Context> contexts_at(const Document& doc, NodeId id, NodeId* code) {
  const SyntaxTree& tree = doc.tree();
  std::vector<NodeId> nodes;
  *code = no_node;
  for (NodeId cur = id; cur != no_node; cur = tree[cur].parent) {
    const NodeKind k = tree[cur].kind;
    if (k == NodeKind::code_block && *code == no_node) *code = cur;
    if (k == NodeKind::list_item || k == NodeKind::block_quote) nodes.push_back(cur);
  }
  std::vector<Context> out;
  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
    if (auto c = make_context(doc, *it)) out.push_back(std::move(*c));
  }
  return out;
}

std::vector<Context> contexts_at_line(const Document& doc, std::size_t line) {
  const LineText lt = line_text(doc.buffer(), line);
  NodeId code = no_node;
  return contexts_at(doc, doc.tree().node_at(lt.text.empty() ? lt.start : lt.end() - 1), &code);
}

std::string quote_prefix(const std::vector<Context>& ctx) {
  std::string r;
  for (const Context& c : ctx) {
    if (c.quote) r += '>';
  }
  return r;
}

std::string continuation_prefix(const std::vector<Context>& ctx) {
  std::string r;
  for (std::size_t i = 0; i < ctx.size(); ++i) {
    if (r.size() < ctx[i].from) r.append(ctx[i].from - r.size(), ' ');
    const bool last = i + 1 == ctx.size();
    std::optional<std::size_t> width;
    if (!last) width = ctx[i + 1].from > r.size() ? ctx[i + 1].from - r.size() : 0;
    r += ctx[i].blank(width);
    if (last && !ctx[i].quote && ctx[i].after.empty()) r += ' ';
  }
  return r;
}

std::string_view line_ending_of(const Buffer& buf, std::size_t line) {
  if (line + 1 >= buf.line_count()) return "\n";
  const std::size_t next = buf.line_start(line + 1);
  return next >= 2 && buf.at(next - 2) == '\r' ? "\r\n" : "\n";
}

NodeId node_before(const Document& doc, std::size_t pos) { return doc.tree().node_at(pos > 0 ? pos - 1 : 0); }

std::size_t last_line_of(const Document& doc, NodeId id) {
  const Node& n = doc.tree()[id];
  return doc.buffer().line_of(n.end > n.start ? n.end - 1 : n.start);
}

std::size_t map_position(std::size_t pos, const std::vector<Edit>& ascending) {
  std::ptrdiff_t shift = 0;
  for (const Edit& e : ascending) {
    if (e.offset > pos) break;
    if (e.offset + e.removed <= pos) {
      shift += static_cast<std::ptrdiff_t>(e.inserted.size()) - static_cast<std::ptrdiff_t>(e.removed);
    } else {
      return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(e.offset) + shift);
    }
  }
  return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(pos) + shift);
}

bool is_space(char c) { return c == ' ' || c == '\t'; }

bool all_space(std::string_view s) {
  return std::all_of(s.begin(), s.end(), [](char c) { return is_space(c) || c == '\r'; });
}

std::size_t first_line_of(const Document& doc, NodeId id) { return doc.buffer().line_of(doc.tree()[id].start); }

bool space_byte(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

void apply_to(std::string& text, const std::vector<Edit>& edits) {
  for (const Edit& e : edits) text.replace(e.offset, e.removed, e.inserted);
}

CommandResult from_ascending(std::vector<Edit> ascending, Selection sel) {
  CommandResult r;
  r.selection = {map_position(sel.anchor, ascending), map_position(sel.cursor, ascending)};
  std::reverse(ascending.begin(), ascending.end());
  r.edits = std::move(ascending);
  return r;
}

std::size_t container_end_column(const std::string& text, const std::vector<Context>& ctx, const Document& doc,
                                 std::size_t line) {
  std::size_t i = 0;
  for (const Context& c : ctx) {
    while (i < text.size() && is_space(text[i])) ++i;
    if (c.quote) {
      if (i < text.size() && text[i] == '>') ++i;
    } else if (first_line_of(doc, c.node) == line) {
      i = std::max(i, std::min(c.to, text.size()));
    }
  }
  return i;
}

std::size_t content_column(const std::string& text, const std::vector<Context>& ctx, const Document& doc,
                           std::size_t line) {
  std::size_t i = container_end_column(text, ctx, doc, line);
  while (i < text.size() && is_space(text[i])) ++i;
  return i;
}

BlockLine block_line(const Document& doc, std::size_t line) {
  const SyntaxTree& tree = doc.tree();
  BlockLine b{line_text(doc.buffer(), line)};
  b.col = content_column(b.lt.text, contexts_at_line(doc, line), doc, line);
  if (b.blank()) return b;
  const NodeId at = tree.node_at(b.lt.start + b.col);
  for (NodeId cur = at; cur != no_node; cur = tree[cur].parent) {
    if (tree[cur].kind == NodeKind::paragraph || tree[cur].kind == NodeKind::heading) {
      b.block = cur;
      break;
    }
  }
  b.underline = tree[at].kind == NodeKind::heading_marker && first_line_of(doc, tree[at].parent) < line;
  return b;
}

std::optional<Edit> blank_before_at(const Document& doc, std::size_t line, const std::vector<Context>& ctx) {
  if (line == 0 || block_line(doc, line - 1).blank()) return std::nullopt;
  std::string ins = quote_prefix(ctx);
  ins += line_ending_of(doc.buffer(), line - 1);
  return Edit{doc.buffer().line_start(line), 0, std::move(ins)};
}

std::optional<Edit> blank_after_at(const Document& doc, std::size_t line, const std::vector<Context>& ctx) {
  const Buffer& buf = doc.buffer();
  if (line + 1 >= buf.line_count() || block_line(doc, line + 1).blank()) return std::nullopt;
  std::string ins = quote_prefix(ctx);
  ins += line_ending_of(buf, line);
  return Edit{buf.line_start(line + 1), 0, std::move(ins)};
}

std::pair<std::size_t, std::size_t> touched_lines(const Document& doc, const std::vector<Edit>& edits) {
  const Buffer& buf = doc.buffer();
  std::size_t first = buf.line_count();
  std::size_t last = 0;
  for (const Edit& e : edits) {
    const std::size_t end = e.offset + e.removed;
    first = std::min(first, buf.line_of(e.offset));
    last = std::max(last, buf.line_of(e.removed > 0 && buf.line_start(buf.line_of(end)) == end ? end - 1 : end));
  }
  for (const Node& n : doc.tree().nodes()) {
    if ((n.kind != NodeKind::code_block && n.kind != NodeKind::heading) || n.end <= n.start) continue;
    const std::size_t a = buf.line_of(n.start);
    const std::size_t b = buf.line_of(n.end - 1);
    if (a <= last && b >= first) {
      first = std::min(first, a);
      last = std::max(last, b);
    }
  }
  return {first, last};
}

bool leaf_block(NodeKind k) {
  return k == NodeKind::paragraph || k == NodeKind::heading || k == NodeKind::code_block ||
         k == NodeKind::thematic_break || k == NodeKind::table || k == NodeKind::html_block ||
         k == NodeKind::link_reference_definition || k == NodeKind::front_matter;
}

bool container_block(NodeKind k) {
  return k == NodeKind::document || k == NodeKind::list || k == NodeKind::list_item || k == NodeKind::block_quote;
}

std::optional<LineRole> line_role(const Document& doc, std::size_t line, std::size_t tf, std::size_t tl) {
  const LineText lt = line_text(doc.buffer(), line);
  std::size_t p = lt.text.size();
  while (p > 0 && space_byte(lt.text[p - 1])) --p;
  if (p == 0) return std::nullopt;
  const SyntaxTree& tree = doc.tree();
  LineRole role;
  for (NodeId cur = tree.node_at(lt.start + p - 1); cur != no_node; cur = tree[cur].parent) {
    const NodeKind k = tree[cur].kind;
    if (role.containers.empty() && role.leaf == NodeKind::document && leaf_block(k)) {
      role.leaf = k;
      role.reaches = first_line_of(doc, cur) <= tl && last_line_of(doc, cur) >= tf;
    } else if (container_block(k)) {
      role.containers.push_back(k);
    }
  }
  return role;
}

bool keeps_outside(const Document& doc, const CommandResult& r, std::size_t first, std::size_t last) {
  if (r.edits.empty()) return true;
  std::string after = doc.text();
  apply_to(after, r.edits);
  const Document check(std::move(after), doc.dialect());
  const std::size_t old_lines = doc.buffer().line_count();
  const std::size_t added = check.buffer().line_count() - old_lines;  
  for (std::size_t line = 0; line < old_lines; ++line) {
    if (line >= first && line <= last) continue;
    const auto was = line_role(doc, line, first, last);
    if (!was) continue;
    const auto now = line_role(check, line < first ? line : line + added, first, last + added);
    if (!now || was->leaf != now->leaf || was->containers != now->containers || (!was->reaches && now->reaches))
      return false;
  }
  return true;
}

std::optional<CommandResult> with_typed(const Document& doc, const CommandResult& r, std::string_view typed) {
  if (r.edits.empty()) return std::nullopt;
  std::size_t start = doc.size();
  std::size_t old_end = 0;
  for (const Edit& e : r.edits) {
    start = std::min(start, e.offset);
    old_end = std::max(old_end, e.offset + e.removed);
  }
  std::string after = doc.text();
  apply_to(after, r.edits);  
  const std::size_t new_end = after.size() - (doc.size() - old_end);
  const std::size_t c = r.selection.cursor;
  if (c < start || c > new_end) return std::nullopt;
  std::string region = after.substr(start, new_end - start);
  region.insert(c - start, typed);
  return CommandResult{{Edit{start, old_end - start, std::move(region)}}, {c + typed.size(), c + typed.size()}};
}

std::optional<CommandResult> resolve_blanks(const Document& doc, BlockDraft draft, std::string_view event,
                                            std::string_view kind) {
  const auto [first, last] = touched_lines(doc, draft.edits);
  if (!draft.blank_before) draft.blank_before = blank_before_at(doc, first, contexts_at_line(doc, first));
  if (!draft.blank_after) draft.blank_after = blank_after_at(doc, last, contexts_at_line(doc, last));
  for (const auto& [before, after] : {std::pair{false, false}, {true, false}, {false, true}, {true, true}}) {
    if ((before && !draft.blank_before) || (after && !draft.blank_after)) continue;
    std::vector<Edit> edits = draft.edits;
    if (before) edits.insert(edits.begin(), *draft.blank_before);
    if (after) edits.push_back(*draft.blank_after);
    CommandResult r = from_ascending(std::move(edits), draft.sel);
    if (draft.after) {
      const std::size_t d = before ? draft.blank_before->inserted.size() : 0;
      r.selection = {draft.after->anchor + d, draft.after->cursor + d};
    }
    if (!keeps_outside(doc, r, first, last)) continue;
    if (draft.check_typed) {
      const auto typed = with_typed(doc, r, "x");
      if (!typed || !keeps_outside(doc, *typed, first, last)) continue;
    }
    log_decision(event, kind, r.edits.size());
    return r;
  }
  log_decision(event, "changes_outside", 0);
  return std::nullopt;
}

bool line_in_kind(const Document& doc, std::size_t line, NodeKind k) {
  const LineText lt = line_text(doc.buffer(), line);
  const std::size_t probe = lt.text.empty() ? lt.start : lt.end() - 1;
  for (NodeId cur = doc.tree().node_at(probe); cur != no_node; cur = doc.tree()[cur].parent) {
    if (doc.tree()[cur].kind == k) return true;
  }
  return false;
}

std::vector<Context> empty_line_contexts(const Document& doc, std::size_t line_no, const LineText& lt) {
  std::vector<Context> ctx = contexts_at_line(doc, line_no);
  if (!ctx.empty() || lt.text.empty() || !all_space(lt.text)) return ctx;
  std::size_t above = line_no;
  while (above > 0 && all_space(line_text(doc.buffer(), above - 1).text)) --above;
  if (above == 0) return ctx;
  for (const Context& c : contexts_at_line(doc, above - 1)) {
    if (c.quote || c.to > lt.text.size()) break;
    ctx.push_back(c);
  }
  return ctx;
}

std::string blank_line_pad(const Document& doc, std::size_t line) {
  const LineText lt = line_text(doc.buffer(), line);
  if (!all_space(lt.text)) return {};
  const std::string prefix = continuation_prefix(empty_line_contexts(doc, line, lt));
  if (!all_space(prefix) || prefix.size() <= lt.text.size()) return {};
  return prefix.substr(lt.text.size());
}

bool node_starts_at(const Document& doc, const std::vector<Edit>& ascending, std::size_t node_start, NodeKind k,
                    std::size_t depth) {
  std::string after = doc.text();
  std::vector<Edit> descending = ascending;
  std::reverse(descending.begin(), descending.end());
  apply_to(after, descending);
  const Document check(std::move(after), doc.dialect());
  const SyntaxTree& tree = check.tree();
  for (NodeId cur = tree.node_at(node_start); cur != no_node; cur = tree[cur].parent) {
    if (tree[cur].kind != k) continue;
    if (tree[cur].start != node_start) return false;
    std::size_t n = 0;
    for (NodeId up = tree[cur].parent; up != no_node; up = tree[up].parent) {
      if (tree[up].kind == NodeKind::list_item || tree[up].kind == NodeKind::block_quote) ++n;
    }
    return n == depth;
  }
  return false;
}

std::optional<CommandResult> place_block(const Document& doc, const Edit& core, const std::vector<Context>& ctx,
                                         std::size_t node_start, NodeKind k, std::size_t target,
                                         std::string_view event, std::string_view kind) {
  const auto [first, last] = touched_lines(doc, {core});
  const std::optional<Edit> before = blank_before_at(doc, first, contexts_at_line(doc, first));
  const std::optional<Edit> after_blank = blank_after_at(doc, last, ctx);
  for (const auto& [use_before, use_after] : {std::pair{false, false}, {true, false}, {false, true}, {true, true}}) {
    if ((use_before && !before) || (use_after && !after_blank)) continue;
    std::vector<Edit> ascending = {core};
    if (use_before) ascending.insert(ascending.begin(), *before);
    if (use_after) ascending.push_back(*after_blank);
    CommandResult out = from_ascending(ascending, Selection{core.offset, core.offset});
    if (!keeps_outside(doc, out, first, last)) continue;
    const std::size_t d = use_before ? before->inserted.size() : 0;
    if (!node_starts_at(doc, ascending, node_start + d, k, ctx.size())) continue;
    out.selection = {target + d, target + d};
    log_decision(event, kind, out.edits.size());
    return out;
  }
  log_decision(event, "changes_outside", 0);
  return std::nullopt;
}

}  

using namespace detail;

namespace {

std::string num(std::uint64_t v) { return std::to_string(v); }

CommandResult joined(const Document& doc, std::vector<Edit> ascending, Selection sel) {
  if (ascending.size() < 2) return from_ascending(std::move(ascending), sel);
  const std::size_t lo = ascending.front().offset;
  std::size_t hi = lo;
  for (const Edit& e : ascending) hi = std::max(hi, e.offset + e.removed);
  std::string text = doc.buffer().slice(lo, hi - lo);
  for (auto it = ascending.rbegin(); it != ascending.rend(); ++it) {
    text.replace(it->offset - lo, it->removed, it->inserted);
  }
  CommandResult r;
  r.selection = {map_position(sel.anchor, ascending), map_position(sel.cursor, ascending)};
  r.edits.push_back(Edit{lo, hi - lo, std::move(text)});
  return r;
}

void replace_marker(const Document& doc, const Context& c, std::string mark, std::vector<Edit>& edits) {
  const Buffer& buf = doc.buffer();
  const std::size_t first = first_line_of(doc, c.node);
  const std::size_t old_len = c.mark_end - c.from;
  const auto shift = static_cast<std::ptrdiff_t>(mark.size()) - static_cast<std::ptrdiff_t>(old_len);
  edits.push_back(Edit{buf.line_start(first) + c.from, old_len, std::move(mark)});
  if (shift == 0) return;
  for (std::size_t l = first + 1, last = last_line_of(doc, c.node); l <= last; ++l) {
    const LineText line = line_text(buf, l);
    if (all_space(line.text) || line.text.size() < c.from) continue;
    if (shift > 0) {
      edits.push_back(Edit{line.start + c.from, 0, std::string(static_cast<std::size_t>(shift), ' ')});
    } else {
      std::size_t n = 0;
      while (n < static_cast<std::size_t>(-shift) && c.from + n < line.text.size() && line.text[c.from + n] == ' ') ++n;
      if (n > 0) edits.push_back(Edit{line.start + c.from, n, {}});
    }
  }
}

void renumber_after(const Document& doc, NodeId item, std::uint64_t number, std::vector<Edit>& edits) {
  constexpr std::uint64_t max_number = 999'999'999;  
  for (NodeId cur = doc.tree()[item].next_sibling; cur != no_node; cur = doc.tree()[cur].next_sibling) {
    if (doc.tree()[cur].kind != NodeKind::list_item) continue;  
    const auto c = make_context(doc, cur);
    if (!c || c->number != number + 1 || c->number + 1 > max_number) return;
    replace_marker(doc, *c, num(c->number + 1) + c->bullet, edits);
    number = c->number;
  }
}

std::optional<CommandResult> code_newline(const Document& doc, std::size_t pos, const std::vector<Context>& ctx,
                                          std::string_view newline, std::string_view event) {
  const std::size_t line_no = doc.buffer().line_of(pos);
  const LineText line = line_text(doc.buffer(), line_no);
  std::string t = line.text;
  std::size_t i = 0;
  auto skip_space = [&] {
    while (i < t.size() && is_space(t[i])) ++i;
  };
  for (const Context& c : ctx) {
    skip_space();
    if (c.quote) {
      if (i < t.size() && t[i] == '>') ++i;
    } else if (first_line_of(doc, c.node) == line_no) {
      for (std::size_t j = i; j < c.to && j < t.size(); ++j) {
        if (t[j] != '\t') t[j] = ' ';
      }
      i = std::max(i, std::min(c.to, t.size()));
    }
  }
  skip_space();
  const std::size_t k = std::min(i, pos - line.start);
  if (k == 0) return std::nullopt;
  std::string insert(newline);
  insert += t.substr(0, k);
  CommandResult r;
  r.selection.cursor = r.selection.anchor = pos + insert.size();
  r.edits.push_back(Edit{pos, 0, std::move(insert)});
  log_decision(event, "code", 1);
  return r;
}

CommandResult at_cursor(std::vector<Edit> edits, std::size_t cursor) {
  CommandResult r;
  r.edits = std::move(edits);
  r.selection = {cursor, cursor};
  return r;
}

}  

std::optional<CommandResult> insert_newline(const Document& doc, Selection sel, std::string_view newline) {
  if (sel.anchor != sel.cursor) return std::nullopt;
  const std::size_t pos = sel.cursor;
  const Buffer& buf = doc.buffer();
  const std::size_t line_no = buf.line_of(pos);
  const LineText line = line_text(buf, line_no);
  NodeId code = no_node;
  std::vector<Context> ctx = contexts_at(doc, node_before(doc, pos), &code);
  if (code != no_node) return code_newline(doc, pos, ctx, newline, "enter");

  const std::size_t col = pos - line.start;
  while (!ctx.empty() && ctx.back().from > col) ctx.pop_back();
  if (ctx.empty()) return std::nullopt;
  const Context& inner = ctx.back();
  if (inner.to - inner.after.size() > col) return std::nullopt;
  const bool empty_line =
      col >= inner.to - inner.after.size() && all_space(std::string_view(line.text).substr(std::min(inner.to, line.text.size())));

  if (!inner.quote && empty_line) {
    const Context* next = ctx.size() > 1 ? &ctx[ctx.size() - 2] : nullptr;
    std::size_t del_to = line.start;
    std::string insert;
    if (next && !next->quote) {
      del_to += next->from;
      insert = next->marker();
    } else if (next) {
      del_to += next->to;
    }
    std::vector<Edit> edits = {Edit{del_to, pos - del_to, std::move(insert)}};
    if (next && next->ordered) renumber_after(doc, next->node, next->number, edits);
    log_decision("enter", "end_item", edits.size());
    return joined(doc, std::move(edits), sel);
  }

  if (inner.quote && empty_line && line_no > 0) {
    const LineText prev = line_text(buf, line_no - 1);
    const std::size_t gt = prev.text.find_last_of('>');
    if (gt != std::string::npos && gt == inner.from && all_space(std::string_view(prev.text).substr(gt + 1))) {
      const std::size_t first_from = prev.start + gt;
      const std::size_t first_len = prev.end() - first_from;
      const std::size_t second_from = line.start + inner.from;
      std::vector<Edit> edits = {Edit{second_from, line.end() - second_from, {}}, Edit{first_from, first_len, {}}};
      log_decision("enter", "end_quote", 2);
      return at_cursor(std::move(edits), second_from - first_len);
    }
  }

  const bool continued = !inner.quote && doc.tree()[inner.node].start < line.start;
  std::size_t lead = 0;
  while (lead < line.text.size() && std::string_view(" \t0123456789.)-+*>").find(line.text[lead]) != std::string_view::npos)
    ++lead;
  std::string insert;
  if (!continued || lead >= inner.to) {
    for (std::size_t i = 0; i < ctx.size(); ++i) {
      const bool last = i + 1 == ctx.size();
      if (insert.size() < ctx[i].from) insert.append(ctx[i].from - insert.size(), ' ');
      if (last && !continued) {
        insert += ctx[i].marker();
      } else {
        std::optional<std::size_t> width;
        if (!last) width = ctx[i + 1].from > insert.size() ? ctx[i + 1].from - insert.size() : 0;
        insert += ctx[i].blank(width);
      }
    }
  }
  std::size_t from = pos;
  while (from > line.start && is_space(line.text[from - 1 - line.start])) --from;
  std::string text(newline);
  if (!inner.quote && !doc.tree()[inner.list].tight) {
    std::string blank;
    for (std::size_t i = 0; i + 1 < ctx.size(); ++i) {
      const bool last = i + 2 == ctx.size();
      if (blank.size() < ctx[i].from) blank.append(ctx[i].from - blank.size(), ' ');
      std::optional<std::size_t> width;
      if (!last) width = ctx[i + 1].from > blank.size() ? ctx[i + 1].from - blank.size() : 0;
      blank += ctx[i].blank(width, !last);
    }
    text += blank;
    text += newline;
  }
  text += insert;
  std::vector<Edit> edits = {Edit{from, pos - from, std::move(text)}};
  if (inner.ordered && !continued) renumber_after(doc, inner.node, inner.number, edits);
  log_decision("enter", inner.quote ? "continue_quote" : "continue_item", edits.size());
  return joined(doc, std::move(edits), sel);
}

namespace {

std::vector<NodeId> selected_items(const Document& doc, Selection sel) {
  const SyntaxTree& tree = doc.tree();
  const Buffer& buf = doc.buffer();
  const std::size_t lo = std::min(sel.anchor, sel.cursor);
  const std::size_t hi = std::max(sel.anchor, sel.cursor);
  const std::size_t first_line = buf.line_of(lo);
  std::size_t last_line = buf.line_of(hi);
  if (hi > lo && last_line > first_line && buf.line_start(last_line) == hi) --last_line;
  const std::size_t range_start = buf.line_start(first_line);
  const std::size_t range_end = last_line + 1 < buf.line_count() ? buf.line_start(last_line + 1) : buf.size() + 1;

  std::vector<NodeId> items;
  const auto nodes = tree.nodes();
  auto it = std::lower_bound(nodes.begin(), nodes.end(), range_start,
                             [](const Node& n, std::size_t v) { return n.start < v; });
  for (; it != nodes.end() && it->start < range_end; ++it) {
    if (it->kind != NodeKind::list_item) continue;
    const NodeId id = static_cast<NodeId>(it - nodes.begin());
    if (!items.empty() && id < tree[items.back()].subtree_end) continue;
    items.push_back(id);
  }
  if (items.empty()) {
    for (NodeId cur = tree.node_at(lo); cur != no_node; cur = tree[cur].parent) {
      if (tree[cur].kind == NodeKind::list_item) {
        items.push_back(cur);
        break;
      }
    }
  }
  return items;
}

NodeId previous_item(const SyntaxTree& tree, NodeId item) {
  NodeId prev = no_node;
  for (NodeId child : tree.children(tree[item].parent)) {
    if (child == item) return prev;
    if (tree[child].kind == NodeKind::list_item) prev = child;
  }
  return no_node;
}

NodeId parent_item(const SyntaxTree& tree, NodeId item) {
  const NodeId list = tree[item].parent;
  if (list == no_node) return no_node;
  const NodeId up = tree[list].parent;
  return up != no_node && tree[up].kind == NodeKind::list_item ? up : no_node;
}

std::size_t prefix_run(const std::string& t) {
  std::size_t i = 0;
  while (i < t.size() && (is_space(t[i]) || t[i] == '>')) ++i;
  return i;
}

std::size_t spaces_before(const std::string& t, std::size_t col) {
  std::size_t n = 0;
  while (n < col && t[col - n - 1] == ' ') ++n;
  return n;
}

}  

std::optional<CommandResult> indent_list(const Document& doc, Selection sel) {
  const SyntaxTree& tree = doc.tree();
  const std::vector<NodeId> items = selected_items(doc, sel);
  if (items.empty()) return std::nullopt;
  std::vector<Edit> edits;
  for (NodeId item : items) {
    const NodeId prev = previous_item(tree, item);
    const auto self = make_context(doc, item);
    const auto above = prev == no_node ? std::nullopt : make_context(doc, prev);
    if (!self || !above) {
      log_decision("indent", "first_item", 0);
      return CommandResult{{}, sel};
    }
    const std::size_t col = self->from;
    const std::size_t width = above->to > col ? above->to - col : 1;
    const std::string pad(width, ' ');
    const bool to_one = self->ordered && self->number != 1;
    const std::size_t mark_width = self->mark_end - self->from;
    const std::size_t lost = to_one ? mark_width - 1 - self->bullet.size() : 0;
    const std::size_t first = first_line_of(doc, item);
    for (std::size_t l = first, last = last_line_of(doc, item); l <= last; ++l) {
      const LineText line = line_text(doc.buffer(), l);
      const std::size_t run = prefix_run(line.text);
      if (l != first && run == line.text.size()) continue;  
      if (l == first) {
        edits.push_back(to_one ? Edit{line.start + col, mark_width, pad + "1" + self->bullet}
                               : Edit{line.start + col, 0, pad});
      } else if (width >= lost) {
        edits.push_back(Edit{line.start + std::min(run, col), 0, std::string(width - lost, ' ')});
      } else {
        const std::size_t n = std::min(lost - width, spaces_before(line.text, run));
        if (n > 0) edits.push_back(Edit{line.start + run - n, n, {}});
      }
    }
  }
  log_decision("indent", "items", edits.size());
  return joined(doc, std::move(edits), sel);
}

std::optional<CommandResult> outdent_list(const Document& doc, Selection sel) {
  const SyntaxTree& tree = doc.tree();
  const std::vector<NodeId> items = selected_items(doc, sel);
  if (items.empty()) return std::nullopt;
  std::vector<Edit> edits;
  for (NodeId item : items) {
    const auto self = make_context(doc, item);
    if (!self) continue;
    const std::size_t col = self->from;
    const LineText first_text = line_text(doc.buffer(), first_line_of(doc, item));
    std::size_t width = 0;
    const NodeId parent = parent_item(tree, item);
    if (parent != no_node) {
      const auto up = make_context(doc, parent);
      if (up && up->from < col) width = col - up->from;
    } else {
      width = spaces_before(first_text.text, col);
    }
    if (width == 0) continue;
    const std::size_t first = first_line_of(doc, item);
    for (std::size_t l = first, last = last_line_of(doc, item); l <= last; ++l) {
      const LineText line = line_text(doc.buffer(), l);
      const std::size_t run = prefix_run(line.text);
      if (l != first && run == line.text.size()) continue;  
      const std::size_t at = l == first ? col : run;
      const std::size_t n = std::min(width, spaces_before(line.text, at));
      if (n > 0) edits.push_back(Edit{line.start + at - n, n, {}});
    }
    NodeId next = tree[item].next_sibling;
    while (next != no_node && tree[next].kind != NodeKind::list_item) next = tree[next].next_sibling;
    if (next == no_node || std::find(items.begin(), items.end(), next) != items.end()) continue;
    const auto c = make_context(doc, next);
    if (c && c->ordered && c->number != 1) replace_marker(doc, *c, "1" + c->bullet, edits);
  }
  log_decision("outdent", "items", edits.size());
  return joined(doc, std::move(edits), sel);
}

std::optional<CommandResult> delete_markup_backward(const Document& doc, Selection sel) {
  if (sel.anchor != sel.cursor || sel.cursor == 0) return std::nullopt;
  const std::size_t pos = sel.cursor;
  const LineText line = line_text(doc.buffer(), doc.buffer().line_of(pos));
  NodeId code = no_node;
  const std::vector<Context> ctx = contexts_at(doc, node_before(doc, pos), &code);
  if (code != no_node || ctx.empty()) return std::nullopt;
  const Context& inner = ctx.back();
  const std::size_t col = pos - line.start;
  const std::size_t space_end = inner.to - inner.after.size() + (inner.after.empty() ? 0 : 1);
  const std::string_view t = line.text;
  if (col > space_end && all_space(t.substr(space_end, col - space_end))) {
    log_decision("backspace", "spaces", 1);
    return at_cursor({Edit{line.start + space_end, col - space_end, {}}}, line.start + space_end);
  }
  if (col != space_end) return std::nullopt;
  const bool item = !inner.quote;
  const Node& n = doc.tree()[inner.node];
  if (item && line.start > n.start && !all_space(t.substr(0, std::min(inner.to, t.size())))) return std::nullopt;
  const std::size_t start = line.start + inner.from;
  if (item && doc.tree()[inner.list].start < n.start && line.start <= n.start &&
      !all_space(t.substr(inner.from, inner.to - inner.from))) {
    std::string insert = inner.blank(inner.to - inner.from);
    log_decision("backspace", "marker_to_spaces", 1);
    const std::size_t cursor = start + insert.size();
    return at_cursor({Edit{start, inner.to - inner.from, std::move(insert)}}, cursor);
  }
  if (start >= pos) return std::nullopt;
  log_decision("backspace", inner.quote ? "quote_marker" : "marker", 1);
  return at_cursor({Edit{start, pos - start, {}}}, start);
}

namespace {

bool word_byte(char c) {
  const auto u = static_cast<unsigned char>(c);
  return u >= 0x80 || (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z');
}

bool plain_text(NodeKind k) { return k == NodeKind::text || k == NodeKind::thematic_break; }

NodeId node_near(const Document& doc, std::size_t pos) {
  const Buffer& b = doc.buffer();
  if (pos > 0 && (pos == b.size() || b.at(pos) == '\n' || b.at(pos) == '\r') &&
      pos > b.line_start(b.line_of(pos)))
    return doc.tree().node_at(pos - 1);
  return doc.tree().node_at(pos);
}

NodeKind node_kind(InlineKind k) {
  switch (k) {
    case InlineKind::strong: return NodeKind::strong;
    case InlineKind::emphasis: return NodeKind::emphasis;
    case InlineKind::strikethrough: return NodeKind::strikethrough;
    case InlineKind::code: break;
  }
  return NodeKind::code_span;
}

struct Markers {
  std::size_t open_start = 0, open_end = 0, close_start = 0, close_end = 0;
};

std::optional<Markers> markers_of(const SyntaxTree& tree, NodeId id) {
  NodeId first = no_node;
  NodeId last = no_node;
  for (NodeId c : tree.children(id)) {
    if (first == no_node) first = c;
    last = c;
  }
  if (first == no_node || first == last) return std::nullopt;
  const Node& n = tree[id];
  if (tree[first].start != n.start || tree[last].end != n.end) return std::nullopt;
  return Markers{n.start, tree[first].end, tree[last].start, n.end};
}

std::optional<std::pair<std::size_t, std::size_t>> link_text_of(const SyntaxTree& tree, NodeId id) {
  for (NodeId c : tree.children(id)) {
    if (tree[c].kind == NodeKind::link_text) return std::pair{tree[c].start, tree[c].end};
  }
  const NodeId first = tree.first_child(id);
  if (first == no_node || tree[first].kind != NodeKind::link_delimiter) return std::nullopt;
  return std::pair{tree[first].end, tree[first].end};
}

bool covers(const SyntaxTree& tree, NodeId id, std::size_t lo, std::size_t hi) {
  const Node& n = tree[id];
  if (lo != hi) return n.start <= lo && hi <= n.end;
  if (n.kind == NodeKind::link) return n.start < lo && lo < n.end;
  const auto m = markers_of(tree, id);
  return m && m->open_end <= lo && lo <= m->close_start;
}

NodeId selection_node(const Document& doc, std::size_t lo, std::size_t hi) {
  return lo == hi ? node_near(doc, lo) : doc.tree().node_at(lo);
}

NodeId find_covering(const Document& doc, Selection sel, NodeKind kind) {
  const SyntaxTree& tree = doc.tree();
  const std::size_t lo = std::min(sel.anchor, sel.cursor);
  const std::size_t hi = std::max(sel.anchor, sel.cursor);
  for (NodeId cur = selection_node(doc, lo, hi); cur != no_node; cur = tree[cur].parent) {
    if (tree[cur].kind == kind && covers(tree, cur, lo, hi)) return cur;
    if (is_block(tree[cur].kind)) break;
  }
  return no_node;
}

bool text_block(NodeKind k) {
  return k == NodeKind::paragraph || k == NodeKind::heading || k == NodeKind::table_cell;
}

bool closed_block(NodeKind k) {
  return k == NodeKind::code_block || k == NodeKind::html_block || k == NodeKind::front_matter ||
         k == NodeKind::link_reference_definition || k == NodeKind::table_delimiter_row;
}

struct Spot {
  NodeId block = no_node;
  NodeId container = no_node;
  bool blocked = false;
  bool in_link = false;
};

Spot spot_at(const Document& doc, std::size_t p, NodeId block_node) {
  const SyntaxTree& tree = doc.tree();
  Spot s;
  for (NodeId cur = block_node; cur != no_node; cur = tree[cur].parent) {
    if (closed_block(tree[cur].kind)) s.blocked = true;
    if (text_block(tree[cur].kind) && s.block == no_node) s.block = cur;
  }
  if (p == 0) return s;
  for (NodeId cur = tree.node_at(p - 1); cur != no_node && !is_block(tree[cur].kind); cur = tree[cur].parent) {
    const Node& n = tree[cur];
    if (!(n.start < p && p < n.end)) continue;
    switch (n.kind) {
      case NodeKind::strong:
      case NodeKind::emphasis:
      case NodeKind::strikethrough: {
        const auto m = markers_of(tree, cur);
        if (!m || p < m->open_end || p > m->close_start) s.blocked = true;
        if (s.container == no_node) s.container = cur;
        break;
      }
      case NodeKind::link:
      case NodeKind::image: {
        const auto t = link_text_of(tree, cur);
        if (!t || p < t->first || p > t->second) s.blocked = true;
        if (n.kind == NodeKind::link) s.in_link = true;
        if (s.container == no_node) s.container = cur;
        break;
      }
      case NodeKind::code_span:
      case NodeKind::autolink:
      case NodeKind::html_inline:
      case NodeKind::math:
      case NodeKind::entity:
      case NodeKind::escape:
        s.blocked = true;
        break;
      default:
        break;
    }
  }
  return s;
}

bool block_marker(NodeKind k) {
  return k == NodeKind::heading_marker || k == NodeKind::list_marker || k == NodeKind::task_marker ||
         k == NodeKind::block_quote_marker || k == NodeKind::block_continuation;
}

std::pair<std::size_t, std::size_t> trim(const Document& doc, std::size_t lo, std::size_t hi) {
  const SyntaxTree& tree = doc.tree();
  const Buffer& b = doc.buffer();
  for (bool moved = true; moved && lo < hi;) {
    moved = false;
    while (lo < hi && space_byte(b.at(lo))) ++lo, moved = true;
    while (lo < hi && space_byte(b.at(hi - 1))) --hi, moved = true;
    if (lo < hi) {
      const Node& a = tree[tree.node_at(lo)];
      if (block_marker(a.kind) && a.end > lo) lo = std::min(a.end, hi), moved = true;
    }
    if (lo < hi) {
      const Node& z = tree[tree.node_at(hi - 1)];
      if (block_marker(z.kind) && z.start < hi) hi = std::max(z.start, lo), moved = true;
    }
  }
  return {lo, hi};
}

std::pair<std::size_t, std::size_t> word_at(const Document& doc, std::size_t p) {
  const LineText line = line_text(doc.buffer(), doc.buffer().line_of(p));
  std::size_t a = p - line.start;
  std::size_t z = a;
  while (a > 0 && word_byte(line.text[a - 1])) --a;
  while (z < line.text.size() && word_byte(line.text[z])) ++z;
  return {line.start + a, line.start + z};
}

std::string code_fence_for(std::string_view text) {
  std::size_t longest = 0;
  for (std::size_t i = 0; i < text.size();) {
    std::size_t run = 0;
    while (i + run < text.size() && text[i + run] == '`') ++run;
    longest = std::max(longest, run);
    i += run == 0 ? 1 : run;
  }
  return std::string(longest + 1, '`');
}

CommandResult wrap(std::size_t lo, std::size_t hi, std::string open, std::string close, Selection sel,
                   bool keep_selection) {
  CommandResult r;
  const std::size_t m = open.size();
  if (keep_selection) {
    const bool forward = sel.anchor <= sel.cursor;
    r.selection = forward ? Selection{lo + m, hi + m} : Selection{hi + m, lo + m};
  } else {
    const std::size_t p = sel.cursor;
    r.selection = {p + m, p + m};
  }
  r.edits.push_back(Edit{hi, 0, std::move(close)});
  r.edits.push_back(Edit{lo, 0, std::move(open)});
  return r;
}

bool can_wrap(const Document& doc, std::size_t lo, std::size_t hi, bool* in_link) {
  const SyntaxTree& tree = doc.tree();
  const Spot a = spot_at(doc, lo, lo == hi ? node_near(doc, lo) : tree.node_at(lo));
  const Spot z = lo == hi ? a : spot_at(doc, hi, tree.node_at(hi - 1));
  if (a.blocked || z.blocked || a.block != z.block || a.container != z.container) return false;
  if (lo != hi && a.block == no_node) return false;
  *in_link = a.in_link;
  return true;
}

std::string link_url(std::string_view url) {
  std::string u;
  for (char c : url) {
    if (c != '\n' && c != '\r') u += c;
  }
  if (u.find_first_of(" \t()<>") == std::string::npos) return u;
  std::string out = "<";
  for (char c : u) {
    if (c == '<' || c == '>') out += '\\';
    out += c;
  }
  return out + ">";
}

}  

std::optional<CommandResult> insert_line_break(const Document& doc, Selection sel, std::string_view newline) {
  if (sel.anchor != sel.cursor) return std::nullopt;
  const std::size_t pos = sel.cursor;
  const Buffer& buf = doc.buffer();
  const SyntaxTree& tree = doc.tree();
  const std::size_t line_no = buf.line_of(pos);
  const LineText line = line_text(buf, line_no);
  const std::size_t col = pos - line.start;
  if (col == 0) return std::nullopt;
  const NodeId at = node_before(doc, pos);
  for (NodeId cur = at; cur != no_node; cur = tree[cur].parent) {
    if (tree[cur].kind == NodeKind::table && line_no <= last_line_of(doc, cur)) {
      log_decision("line_break", "table", 0);
      return std::nullopt;
    }
  }
  NodeId code = no_node;
  const std::vector<Context> ctx = contexts_at(doc, at, &code);
  if (code != no_node) return code_newline(doc, pos, ctx, newline, "line_break");

  std::size_t text_start = prefix_run(line.text);
  if (col < text_start || block_marker(tree[tree.node_at(pos)].kind)) return std::nullopt;
  for (const Context& c : ctx) {
    if (first_line_of(doc, c.node) != line_no) continue;
    if (col < c.to - c.after.size()) return std::nullopt;
    text_start = std::max(text_start, c.to);
  }
  std::size_t from = pos;
  while (from > line.start && is_space(line.text[from - 1 - line.start])) --from;
  const bool text_before = from - line.start > text_start;
  if (!text_before && !all_space(std::string_view(line.text).substr(col))) return std::nullopt;

  bool hard = false;
  if (text_before) {
    const Spot s = spot_at(doc, pos, at);
    if (s.block != no_node && tree[s.block].kind == NodeKind::paragraph && !s.blocked) {
      std::size_t run = 0;
      while (from - run > line.start && buf.at(from - run - 1) == '\\') ++run;
      hard = run % 2 == 0;
    }
  }

  std::string prefix;
  for (std::size_t i = 0; i < ctx.size(); ++i) {
    Context c = ctx[i];
    if (!c.quote) {
      const std::string first = line_text(buf, first_line_of(doc, c.node)).text;
      std::size_t n = 0;
      while (c.mark_end + n < first.size() && first[c.mark_end + n] == ' ') ++n;
      if (n == 0 || n >= 5 || c.mark_end + n >= first.size()) n = 1;
      c.to = c.mark_end + n;
      c.after.assign(n, ' ');
    }
    if (prefix.size() < c.from) prefix.append(c.from - prefix.size(), ' ');
    std::optional<std::size_t> width;
    if (i + 1 < ctx.size()) width = ctx[i + 1].from > prefix.size() ? ctx[i + 1].from - prefix.size() : 0;
    prefix += c.blank(width);
  }
  if (prefix.empty() && !hard) return std::nullopt;

  std::string text = hard ? "\\" : "";
  text += newline;
  text += prefix;
  std::vector<Edit> edits = {Edit{from, pos - from, std::move(text)}};
  log_decision("line_break", hard ? "hard_break" : "newline", 1);
  return from_ascending(std::move(edits), sel);
}

namespace {

std::optional<CommandResult> remove_node(const Document& doc, Selection sel, NodeKind kind, bool* found) {
  const SyntaxTree& tree = doc.tree();
  const NodeId n = find_covering(doc, sel, kind);
  *found = n != no_node;
  if (n == no_node) return std::nullopt;
  std::size_t open_end = 0;
  std::size_t close_start = 0;
  if (kind == NodeKind::link) {
    const auto t = link_text_of(tree, n);
    if (!t) return std::nullopt;
    open_end = t->first;
    close_start = t->second;
  } else {
    const auto m = markers_of(tree, n);
    if (!m) return std::nullopt;
    open_end = m->open_end;
    close_start = m->close_start;
    if (kind == NodeKind::code_span && close_start >= open_end + 2) {
      const std::string inner = doc.buffer().slice(open_end, close_start - open_end);
      const bool all_spaces = inner.find_first_not_of(' ') == std::string::npos;
      if (!all_spaces && inner.front() == ' ' && inner.back() == ' ') {
        ++open_end;
        --close_start;
      }
    }
  }
  std::vector<Edit> edits = {Edit{tree[n].start, open_end - tree[n].start, {}},
                             Edit{close_start, tree[n].end - close_start, {}}};
  return from_ascending(std::move(edits), sel);
}

using KindCounts = std::array<long, node_kind_count>;

KindCounts kind_counts(const SyntaxTree& tree) {
  KindCounts c{};
  for (const Node& n : tree.nodes()) {
    if (n.kind != NodeKind::text) ++c[static_cast<std::size_t>(n.kind)];
  }
  return c;
}

void count(KindCounts& c, NodeKind k) { ++c[static_cast<std::size_t>(k)]; }

bool wrap_parses(const Document& doc, const CommandResult& r, NodeKind kind) {
  const std::string before = doc.text();
  std::string after = before;
  apply_to(after, r.edits);
  const Document check(after, doc.dialect());
  const SyntaxTree& tree = check.tree();
  const NodeId n = find_covering(check, r.selection, kind);
  if (n == no_node) return false;
  KindCounts expected = kind_counts(doc.tree());
  count(expected, kind);
  NodeId first = no_node;
  NodeId last = no_node;
  for (NodeId c : tree.children(n)) {
    if (first == no_node) first = c;
    last = c;
    if (kind == NodeKind::link && tree[c].kind != NodeKind::text)
      count(expected, tree[c].kind);
  }
  if (kind != NodeKind::link) {
    if (first == no_node || first == last) return false;
    count(expected, tree[first].kind);
    count(expected, tree[last].kind);
  }
  if (kind_counts(tree) != expected) return false;
  bool found = false;
  const auto back = remove_node(check, r.selection, kind, &found);
  if (!back) return false;
  apply_to(after, back->edits);
  return after == before;
}

bool counts_match(const KindCounts& got, const KindCounts& want, bool blank_line) {
  if (got == want) return true;
  if (!blank_line) return false;
  for (NodeKind k : {NodeKind::paragraph, NodeKind::thematic_break, NodeKind::soft_break}) {
    KindCounts plus = want;
    count(plus, k);
    if (got == plus) return true;
  }
  return false;
}

template <typename Map>
std::vector<std::tuple<NodeKind, std::size_t, std::size_t>> inline_keys(const SyntaxTree& tree, Map map) {
  std::vector<std::tuple<NodeKind, std::size_t, std::size_t>> out;
  for (const Node& n : tree.nodes()) {
    if (n.kind != NodeKind::text && !is_block(n.kind)) out.emplace_back(n.kind, map(n.start), map(n.end));
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool pair_parses(const Document& doc, std::size_t p, const std::string& pair, bool blank_line) {
  std::string after = doc.text();
  after.insert(p, pair);
  const Document check(after, doc.dialect());
  const SyntaxTree& tree = check.tree();
  for (std::size_t i = p; i < p + pair.size(); ++i) {
    if (!plain_text(tree[tree.node_at(i)].kind)) return false;
  }
  const std::size_t m = pair.size();
  const auto back = [&](std::size_t x) { return x <= p ? x : (x >= p + m ? x - m : p); };
  const auto same = [](std::size_t x) { return x; };
  auto got = inline_keys(tree, back);
  const auto want = inline_keys(doc.tree(), same);
  if (blank_line && got.size() == want.size() + 1) {
    const auto it = std::find_if(got.begin(), got.end(), [&](const auto& k) {
      return std::get<0>(k) == NodeKind::soft_break && std::get<2>(k) == p;
    });
    if (it != got.end()) got.erase(it);
  }
  if (got != want) return false;
  return counts_match(kind_counts(tree), kind_counts(doc.tree()), blank_line);
}

bool pair_is_line(const Document& doc, std::size_t p, std::size_t n) {
  std::string after = doc.text();
  after.erase(p, n);
  const Document check(std::move(after), doc.dialect());
  return block_line(check, check.buffer().line_of(p)).blank();
}

std::optional<CommandResult> checked_wrap(const Document& doc, CommandResult r, NodeKind kind,
                                          std::string_view event) {
  if (!wrap_parses(doc, r, kind)) {
    log_decision(event, "wrap_does_not_parse", 0);
    return std::nullopt;
  }
  log_decision(event, "wrap", r.edits.size());
  return r;
}

}  

std::optional<CommandResult> toggle_inline(const Document& doc, Selection sel, InlineKind kind) {
  const Buffer& buf = doc.buffer();
  const NodeKind nk = node_kind(kind);
  const std::size_t lo0 = std::min(sel.anchor, sel.cursor);
  const std::size_t hi0 = std::max(sel.anchor, sel.cursor);

  bool found = false;
  auto removed = remove_node(doc, sel, nk, &found);
  if (found) {
    log_decision("toggle_inline", "remove", removed ? 2 : 0);
    return removed;
  }

  std::string open;
  switch (kind) {
    case InlineKind::strong: open = "**"; break;
    case InlineKind::emphasis: open = "*"; break;
    case InlineKind::strikethrough: open = "~~"; break;
    case InlineKind::code: open = "`"; break;
  }

  if (lo0 == hi0) {
    const std::size_t p = lo0;
    const std::size_t m = open.size();
    const auto plain = [&](std::size_t i) { return plain_text(doc.tree()[doc.tree().node_at(i)].kind); };
    if (p >= m && p + m <= buf.size() && buf.slice(p - m, m) == open && buf.slice(p, m) == open &&
        ((plain(p - m) && plain(p + m - 1)) || pair_is_line(doc, p - m, 2 * m))) {
      log_decision("toggle_inline", "remove_pair", 1);
      return at_cursor({Edit{p - m, 2 * m, {}}}, p - m);
    }
    bool in_link = false;
    const auto [a, z] = word_at(doc, p);
    if (a < z && can_wrap(doc, a, z, &in_link)) {
      std::string close = open;
      if (kind == InlineKind::code) open = close = code_fence_for(buf.slice(a, z - a));
      return checked_wrap(doc, wrap(a, z, open, close, sel, false), nk, "toggle_inline");
    }
    if (!can_wrap(doc, p, p, &in_link)) return std::nullopt;
    const bool blank_line = block_line(doc, buf.line_of(p)).blank();
    if (!blank_line && !pair_parses(doc, p, open + open, false)) {
      log_decision("toggle_inline", "pair_does_not_parse", 0);
      return std::nullopt;
    }
    log_decision("toggle_inline", "insert_pair", 1);
    return at_cursor({Edit{p, 0, open + open}}, p + m);
  }

  const auto [lo, hi] = trim(doc, lo0, hi0);
  bool in_link = false;
  if (lo >= hi || !can_wrap(doc, lo, hi, &in_link)) return std::nullopt;
  std::string close = open;
  if (kind == InlineKind::code) {
    const std::string text = buf.slice(lo, hi - lo);
    open = close = code_fence_for(text);
    if (text.front() == '`' || text.back() == '`') {
      open += ' ';
      close.insert(close.begin(), ' ');
    }
  }
  return checked_wrap(doc, wrap(lo, hi, open, close, sel, true), nk, "toggle_inline");
}

namespace {

KindCounts inline_counts_in(const SyntaxTree& tree, std::size_t from, std::size_t to) {
  KindCounts c{};
  for (const Node& n : tree.nodes()) {
    if (n.kind != NodeKind::text && !is_block(n.kind) && from <= n.start && n.end <= to)
      ++c[static_cast<std::size_t>(n.kind)];
  }
  return c;
}

std::string link_text(std::string_view text) {
  std::string t;
  long depth = 0;
  bool balanced = true;
  for (std::size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (c == '\\' && i + 1 < text.size()) {
      t += c;
      t += text[++i];
      continue;
    }
    if (c == '\n' || c == '\r') c = ' ';
    if (c == '[') ++depth;
    if (c == ']' && --depth < 0) balanced = false;
    t += c;
  }
  if (balanced && depth == 0) return t;
  std::string out;
  for (std::size_t i = 0; i < t.size(); ++i) {
    if (t[i] == '\\' && i + 1 < t.size()) {
      out += t[i];
      out += t[++i];
      continue;
    }
    if (t[i] == '[' || t[i] == ']') out += '\\';
    out += t[i];
  }
  return out;
}

std::optional<CommandResult> insert_link_node(const Document& doc, Selection sel, std::string_view text,
                                              std::string_view url, NodeKind kind) {
  const bool image = kind == NodeKind::image;
  const char* name = image ? "insert_image" : "insert_link";
  const std::size_t lo0 = std::min(sel.anchor, sel.cursor);
  const std::size_t hi0 = std::max(sel.anchor, sel.cursor);
  auto [lo, hi] = lo0 == hi0 ? std::pair{lo0, hi0} : trim(doc, lo0, hi0);
  if (lo >= hi) lo = hi = lo0;
  bool in_link = false;
  if (!can_wrap(doc, lo, hi, &in_link) || (in_link && !image)) {
    log_decision(name, "not_here", 0);
    return std::nullopt;
  }
  const std::string t = link_text(text);
  const std::string dest = link_url(url);
  const std::string open = image ? "![" : "[";
  const std::string inserted = open + t + "](" + dest + ")";
  std::size_t cursor = lo + inserted.size();
  if (t.empty()) {
    cursor = lo + open.size();
  } else if (dest.empty()) {
    cursor = lo + open.size() + t.size() + 2;
  }

  std::string after = doc.text();
  after.replace(lo, hi - lo, inserted);
  const Document check(after, doc.dialect());
  const SyntaxTree& tree = check.tree();
  const std::size_t end = lo + inserted.size();
  bool found = false;
  for (NodeId cur = tree.node_at(lo); cur != no_node; cur = tree[cur].parent) {
    if (tree[cur].kind == kind && tree[cur].start == lo && tree[cur].end == end) found = true;
  }
  KindCounts expected = kind_counts(doc.tree());
  const KindCounts gone = inline_counts_in(doc.tree(), lo, hi);
  const KindCounts added = inline_counts_in(tree, lo, end);
  for (std::size_t k = 0; k < node_kind_count; ++k) expected[k] += added[k] - gone[k];
  const bool blank_line = lo == hi && block_line(doc, doc.buffer().line_of(lo)).blank();
  if (!found || (!blank_line && !counts_match(kind_counts(tree), expected, false))) {
    log_decision(name, "does_not_parse", 0);
    return std::nullopt;
  }
  log_decision(name, "insert", 1);
  return at_cursor({Edit{lo, hi - lo, inserted}}, cursor);
}

}  

std::optional<CommandResult> insert_link(const Document& doc, Selection sel, std::string_view text,
                                         std::string_view url) {
  return insert_link_node(doc, sel, text, url, NodeKind::link);
}

std::optional<CommandResult> insert_image(const Document& doc, Selection sel, std::string_view alt,
                                          std::string_view url) {
  return insert_link_node(doc, sel, alt, url, NodeKind::image);
}

std::optional<CommandResult> remove_link(const Document& doc, Selection sel) {
  bool found = false;
  auto removed = remove_node(doc, sel, NodeKind::link, &found);
  if (found) log_decision("remove_link", "remove", removed ? 2 : 0);
  return removed;
}

Format format_at(const Document& doc, Selection sel) {
  const SyntaxTree& tree = doc.tree();
  Format f;
  f.strong = find_covering(doc, sel, NodeKind::strong) != no_node;
  f.emphasis = find_covering(doc, sel, NodeKind::emphasis) != no_node;
  f.strikethrough = find_covering(doc, sel, NodeKind::strikethrough) != no_node;
  f.code = find_covering(doc, sel, NodeKind::code_span) != no_node;
  f.link = find_covering(doc, sel, NodeKind::link) != no_node;
  const std::size_t lo = std::min(sel.anchor, sel.cursor);
  const std::size_t hi = std::max(sel.anchor, sel.cursor);
  bool item_seen = false;
  bool list_seen = false;
  for (NodeId cur = selection_node(doc, lo, hi); cur != no_node; cur = tree[cur].parent) {
    const Node& n = tree[cur];
    switch (n.kind) {
      case NodeKind::heading:
        if (f.heading_level == 0) f.heading_level = n.level;
        break;
      case NodeKind::list_item:
        if (!item_seen) {
          item_seen = true;
          for (NodeId c : tree.children(cur)) {
            if (tree[c].kind == NodeKind::task_marker) f.task = true;
          }
        }
        break;
      case NodeKind::list:
        if (!list_seen) {
          list_seen = true;
          f.list = n.ordered ? ListKind::ordered : ListKind::bullet;
        }
        break;
      case NodeKind::block_quote: f.block_quote = true; break;
      case NodeKind::table: f.table = true; break;
      case NodeKind::code_block: f.code_block = true; break;
      default: break;
    }
  }
  return f;
}

namespace {

bool line_is_blocked(const Document& doc, std::size_t line) {
  const LineText lt = line_text(doc.buffer(), line);
  const std::size_t probe = lt.text.empty() ? lt.start : lt.end() - 1;
  for (NodeId cur = doc.tree().node_at(probe); cur != no_node; cur = doc.tree()[cur].parent) {
    switch (doc.tree()[cur].kind) {
      case NodeKind::code_block:
      case NodeKind::table:
      case NodeKind::html_block:
      case NodeKind::thematic_break:
      case NodeKind::front_matter: return true;
      default: break;
    }
  }
  return false;
}

std::optional<std::vector<std::size_t>> block_lines(const Document& doc, Selection sel) {
  const Buffer& buf = doc.buffer();
  const std::size_t lo = std::min(sel.anchor, sel.cursor);
  const std::size_t hi = std::max(sel.anchor, sel.cursor);
  const std::size_t first_line = buf.line_of(lo);
  std::size_t last_line = buf.line_of(hi);
  if (hi > lo && last_line > first_line && buf.line_start(last_line) == hi) --last_line;
  std::size_t from = first_line;
  std::size_t to = last_line;
  for (const std::size_t l : {first_line, last_line}) {
    const LineText lt = line_text(buf, l);
    if (lt.text.empty()) continue;
    for (NodeId cur = doc.tree().node_at(lt.end() - 1); cur != no_node; cur = doc.tree()[cur].parent) {
      if (doc.tree()[cur].kind != NodeKind::heading) continue;
      from = std::min(from, first_line_of(doc, cur));
      to = std::max(to, last_line_of(doc, cur));
      break;
    }
  }
  std::vector<std::size_t> lines;
  for (std::size_t l = from; l <= to; ++l) {
    if (!line_is_blocked(doc, l)) lines.push_back(l);
  }
  if (lines.empty()) return std::nullopt;
  return lines;
}

std::string pad_before(const std::string& text, std::size_t col) {
  return col > 0 && col <= text.size() && !is_space(text[col - 1]) ? " " : "";
}

std::optional<CommandResult> checked_block(const Document& doc, std::optional<CommandResult> r,
                                           std::string_view event) {
  if (!r) return r;
  const auto [first, last] = touched_lines(doc, r->edits);
  if (!keeps_outside(doc, *r, first, last)) {
    log_decision(event, "changes_outside", 0);
    return std::nullopt;
  }
  return r;
}

}  

namespace {

void on_blank_line(const Document& doc, std::size_t line, BlockDraft& draft) {
  const std::size_t end = line_text(doc.buffer(), line).end();
  draft.sel = Selection{end, end};
  draft.check_typed = true;
}

}  

std::optional<CommandResult> set_heading(const Document& doc, Selection sel, int level) {
  const auto lines = block_lines(doc, sel);
  if (!lines) return std::nullopt;
  const SyntaxTree& tree = doc.tree();
  const Buffer& buf = doc.buffer();
  const std::string hashes(static_cast<std::size_t>(level), '#');
  std::vector<Edit> edits;
  std::vector<NodeId> done_setext;
  bool lone_blank = false;  
  std::optional<std::size_t> from_line;
  std::optional<std::size_t> to_line;
  const auto plain_lines = [&](std::size_t a, std::size_t b) {
    from_line = std::min(from_line.value_or(a), a);
    to_line = std::max(to_line.value_or(b), b);
  };

  for (const std::size_t l : *lines) {
    const BlockLine bl = block_line(doc, l);
    if (bl.blank()) {
      if (lines->size() == 1 && sel.anchor == sel.cursor && level > 0) {
        const std::string pad = blank_line_pad(doc, l);
        if (!pad.empty()) {
          edits.push_back(Edit{bl.lt.end(), 0, pad + hashes + " "});
          lone_blank = true;
        } else {
          edits.push_back(Edit{bl.lt.start + bl.col, 0, pad_before(bl.lt.text, bl.col) + hashes + " "});
        }
      }
      continue;
    }
    if (bl.block == no_node) continue;
    const Node& n = tree[bl.block];

    if (n.kind == NodeKind::heading && n.setext) {
      if (std::find(done_setext.begin(), done_setext.end(), bl.block) != done_setext.end()) continue;
      done_setext.push_back(bl.block);
      const std::size_t underline_line = last_line_of(doc, bl.block);
      if (level == 0) plain_lines(first_line_of(doc, bl.block), underline_line);
      if (l != underline_line && level > 0) edits.push_back(Edit{bl.lt.start + bl.col, 0, hashes + " "});
      const LineText ul = line_text(buf, underline_line);
      if (underline_line + 1 < buf.line_count()) {
        edits.push_back(Edit{ul.start, buf.line_start(underline_line + 1) - ul.start, {}});
      } else {
        const std::size_t text_end = line_text(buf, underline_line - 1).end();
        edits.push_back(Edit{text_end, ul.end() - text_end, {}});
      }
      continue;
    }

    if (n.kind == NodeKind::heading) {
      NodeId marker = no_node;
      for (const NodeId c : tree.children(bl.block)) {
        if (tree[c].kind == NodeKind::heading_marker && tree[c].start == n.start) marker = c;
      }
      if (marker == no_node) continue;
      const Node& m = tree[marker];
      if (level > 0) {
        edits.push_back(Edit{m.start, m.end - m.start, hashes});
        continue;
      }
      std::size_t text_start = m.end - bl.lt.start;
      while (text_start < bl.lt.text.size() && is_space(bl.lt.text[text_start])) ++text_start;
      edits.push_back(Edit{m.start, bl.lt.start + text_start - m.start, {}});
      plain_lines(l, l);
      continue;
    }

    if (level > 0) edits.push_back(Edit{bl.lt.start + bl.col, 0, hashes + " "});  
  }

  if (edits.empty()) return CommandResult{{}, sel};
  BlockDraft draft{std::move(edits), sel, std::nullopt, std::nullopt, std::nullopt};
  if (from_line) {
    draft.blank_before = blank_before_at(doc, *from_line, contexts_at_line(doc, *from_line));
    draft.blank_after = blank_after_at(doc, *to_line, contexts_at_line(doc, *to_line));
  }
  if (lone_blank) on_blank_line(doc, lines->front(), draft);
  return resolve_blanks(doc, draft, "set_heading", "lines");
}

namespace {

NodeId innermost_of_kind(const Document& doc, std::size_t lo, std::size_t hi, NodeKind kind) {
  const SyntaxTree& tree = doc.tree();
  for (NodeId cur = node_before(doc, lo); cur != no_node; cur = tree[cur].parent) {
    if (tree[cur].kind == kind && hi <= tree[cur].end) return cur;
  }
  return no_node;
}

NodeId item_starting_at(const Document& doc, std::size_t line) {
  const std::vector<Context> ctx = contexts_at_line(doc, line);
  if (ctx.empty() || ctx.back().quote) return no_node;
  return first_line_of(doc, ctx.back().node) == line ? ctx.back().node : no_node;
}

std::optional<CommandResult> list_off(const Document& doc, Selection sel) {
  const std::vector<NodeId> items = selected_items(doc, sel);
  if (items.empty()) return std::nullopt;
  std::vector<Edit> edits;
  for (const NodeId item : items) {
    const auto self = make_context(doc, item);
    if (!self) continue;
    const std::size_t width = self->to - self->from;
    const std::size_t first = first_line_of(doc, item);
    edits.push_back(Edit{doc.buffer().line_start(first) + self->from, width, {}});
    for (std::size_t l = first + 1, last = last_line_of(doc, item); l <= last; ++l) {
      const LineText line = line_text(doc.buffer(), l);
      const std::size_t run = prefix_run(line.text);
      if (run == line.text.size()) continue;  
      const std::size_t n = std::min(width, spaces_before(line.text, run));
      if (n > 0) edits.push_back(Edit{line.start + run - n, n, {}});
    }
  }
  if (edits.empty()) return std::nullopt;
  const std::size_t first = first_line_of(doc, items.front());
  const std::size_t last = last_line_of(doc, items.back());
  const BlockDraft draft{std::move(edits), sel, blank_before_at(doc, first, contexts_at_line(doc, first)),
                         blank_after_at(doc, last, contexts_at_line(doc, last)), std::nullopt};
  return resolve_blanks(doc, draft, "toggle_block", "list_off");
}

std::optional<CommandResult> list_on(const Document& doc, Selection sel, bool ordered) {
  const auto lines = block_lines(doc, sel);
  if (!lines) return std::nullopt;
  const SyntaxTree& tree = doc.tree();

  if (const NodeId item = item_starting_at(doc, lines->front()); item != no_node) {
    const NodeId list = tree[item].parent;
    if (tree[list].ordered != ordered) {
      std::vector<Edit> edits;
      std::uint64_t next = 1;
      for (const NodeId c : tree.children(list)) {
        if (const auto ctx = make_context(doc, c)) replace_marker(doc, *ctx, ordered ? num(next++) + "." : "-", edits);
      }
      log_decision("toggle_block", "list_convert", edits.size());
      return checked_block(doc, from_ascending(std::move(edits), sel), "toggle_block");
    }
  }

  std::vector<Edit> edits;
  std::optional<std::size_t> last_new;
  char bullet = '-';
  std::uint64_t number = 1;
  std::size_t mark_width = 2;
  const bool lone_blank = lines->size() == 1 && block_line(doc, lines->front()).blank();
  for (const std::size_t l : *lines) {
    if (item_starting_at(doc, l) != no_node) continue;
    const BlockLine bl = block_line(doc, l);
    if (bl.block == no_node && !lone_blank) continue;
    if (bl.underline) {
      edits.push_back(Edit{bl.lt.start + bl.col, 0, std::string(mark_width, ' ')});
      continue;
    }
    if (!last_new) {
      std::size_t above = l;
      if (above > 1 && block_line(doc, above - 1).block == no_node) --above;
      if (const NodeId prev = above > 0 ? item_starting_at(doc, above - 1) : no_node;
          prev != no_node && tree[tree[prev].parent].ordered == ordered) {
        if (const auto pc = make_context(doc, prev)) {
          bullet = pc->bullet.empty() ? '-' : pc->bullet[0];
          number = pc->number + 1;
        }
      }
    }
    last_new = l;
    std::string mark = (ordered ? num(number++) + "." : std::string(1, bullet)) + " ";
    mark_width = mark.size();
    if (lone_blank) mark = blank_line_pad(doc, l) + mark;
    edits.push_back(Edit{bl.lt.start + std::min(bl.col, bl.lt.text.size()), 0, std::move(mark)});
  }

  if (edits.empty()) return CommandResult{{}, sel};
  BlockDraft draft{std::move(edits), sel, std::nullopt, std::nullopt, std::nullopt};
  if (last_new) draft.blank_after = blank_after_at(doc, *last_new, contexts_at_line(doc, *last_new));
  if (lone_blank) on_blank_line(doc, lines->front(), draft);
  return resolve_blanks(doc, draft, "toggle_block", "list_on");
}

std::optional<CommandResult> task_off(const Document& doc, Selection sel) {
  const SyntaxTree& tree = doc.tree();
  std::vector<Edit> edits;
  for (const NodeId item : selected_items(doc, sel)) {
    NodeId box = no_node;
    for (const NodeId c : tree.children(item)) {
      if (tree[c].kind == NodeKind::task_marker) box = c;
    }
    const auto ctx = make_context(doc, item);
    if (box == no_node || !ctx) continue;
    const std::size_t end = doc.buffer().line_start(first_line_of(doc, item)) + ctx->to;
    edits.push_back(Edit{tree[box].start, end - tree[box].start, {}});
  }
  if (edits.empty()) return std::nullopt;
  log_decision("toggle_block", "task_off", edits.size());
  return checked_block(doc, from_ascending(std::move(edits), sel), "toggle_block");
}

std::optional<CommandResult> task_on(const Document& doc, Selection sel) {
  const auto lines = block_lines(doc, sel);
  if (!lines) return std::nullopt;
  std::vector<Edit> edits;
  std::optional<std::size_t> last_new;
  const bool lone_blank = lines->size() == 1 && block_line(doc, lines->front()).blank();
  for (const std::size_t l : *lines) {
    if (const NodeId item = item_starting_at(doc, l); item != no_node) {
      const auto c = make_context(doc, item);
      if (!c || !c->task.empty()) continue;  
      const LineText lt = line_text(doc.buffer(), l);
      edits.push_back(Edit{lt.start + c->to, 0, pad_before(lt.text, c->to) + "[ ] "});
      continue;
    }
    const BlockLine bl = block_line(doc, l);
    if (bl.block == no_node && !lone_blank) continue;
    if (bl.underline) {
      edits.push_back(Edit{bl.lt.start + bl.col, 0, "  "});  
      continue;
    }
    edits.push_back(Edit{bl.lt.start + std::min(bl.col, bl.lt.text.size()), 0,
                         (lone_blank ? blank_line_pad(doc, l) : std::string()) + "- [ ] "});
    last_new = l;
  }

  if (edits.empty()) return CommandResult{{}, sel};
  BlockDraft draft{std::move(edits), sel, std::nullopt, std::nullopt, std::nullopt};
  if (last_new) draft.blank_after = blank_after_at(doc, *last_new, contexts_at_line(doc, *last_new));
  if (lone_blank) on_blank_line(doc, lines->front(), draft);
  return resolve_blanks(doc, draft, "toggle_block", "task_on");
}

std::optional<CommandResult> quote_on(const Document& doc, Selection sel) {
  const auto lines = block_lines(doc, sel);
  if (!lines) return std::nullopt;
  const Buffer& buf = doc.buffer();
  const std::size_t first_line = lines->front();

  const LineText first_lt = line_text(buf, first_line);
  const bool lone_blank = lines->size() == 1 && all_space(first_lt.text);
  std::vector<Context> outer =
      lone_blank ? empty_line_contexts(doc, first_line, first_lt) : contexts_at_line(doc, first_line);
  while (!outer.empty() && first_line_of(doc, outer.back().node) == first_line) outer.pop_back();
  const std::size_t col = container_end_column(first_lt.text, outer, doc, first_line);

  std::size_t last_line = lines->back();
  for (std::size_t l = first_line; l <= last_line; ++l) {
    for (const Context& c : contexts_at_line(doc, l)) {
      if (first_line_of(doc, c.node) >= first_line) last_line = std::max(last_line, last_line_of(doc, c.node));
    }
  }
  std::vector<Edit> edits;
  for (std::size_t l = first_line; l <= last_line; ++l) {
    const LineText lt = line_text(buf, l);
    const std::size_t at = std::min(col, lt.text.size());
    if (lone_blank) {
      edits.push_back(Edit{lt.start + at, 0, blank_line_pad(doc, l) + "> "});
      continue;
    }
    edits.push_back(Edit{lt.start + at, 0, all_space(std::string_view(lt.text).substr(at)) ? ">" : "> "});
  }
  BlockDraft draft{std::move(edits), sel, std::nullopt,
                         blank_after_at(doc, last_line, contexts_at_line(doc, last_line)), std::nullopt};
  if (lone_blank) on_blank_line(doc, first_line, draft);
  return resolve_blanks(doc, draft, "toggle_block", "quote_on");
}

std::vector<Context> contexts_without(const Document& doc, std::size_t line, NodeId drop) {
  std::vector<Context> ctx = contexts_at_line(doc, line);
  ctx.erase(std::remove_if(ctx.begin(), ctx.end(), [&](const Context& c) { return c.node == drop; }), ctx.end());
  return ctx;
}

std::optional<CommandResult> quote_off(const Document& doc, Selection sel) {
  const NodeId q =
      innermost_of_kind(doc, std::min(sel.anchor, sel.cursor), std::max(sel.anchor, sel.cursor), NodeKind::block_quote);
  if (q == no_node) return std::nullopt;
  const std::size_t first = first_line_of(doc, q);
  const std::size_t last = last_line_of(doc, q);
  std::vector<Edit> edits;
  for (std::size_t l = first; l <= last; ++l) {
    const LineText line = line_text(doc.buffer(), l);
    const std::string& t = line.text;
    std::size_t i = 0;
    for (const Context& c : contexts_at_line(doc, l)) {
      while (i < t.size() && is_space(t[i])) ++i;
      if (c.quote) {
        if (i >= t.size() || t[i] != '>') break;
        if (c.node == q) {
          edits.push_back(Edit{line.start + i, i + 1 < t.size() && t[i + 1] == ' ' ? 2U : 1U, {}});
          break;
        }
        ++i;
      } else if (first_line_of(doc, c.node) == l) {
        i = std::max(i, std::min(c.to, t.size()));
      }
    }
  }
  const BlockDraft draft{std::move(edits), sel, blank_before_at(doc, first, contexts_without(doc, first, q)),
                         blank_after_at(doc, last, contexts_without(doc, last, q)), std::nullopt};
  return resolve_blanks(doc, draft, "toggle_block", "quote_off");
}

std::optional<CommandResult> code_on(const Document& doc, Selection sel) {
  const auto lines = block_lines(doc, sel);
  if (!lines) return std::nullopt;
  const Buffer& buf = doc.buffer();
  const std::size_t first_line = lines->front();
  const LineText first_lt = line_text(buf, first_line);
  const LineText last_lt = line_text(buf, lines->back());
  const bool lone_blank = lines->size() == 1 && block_line(doc, first_line).blank();
  const std::vector<Context> ctx =
      lone_blank ? empty_line_contexts(doc, first_line, first_lt) : contexts_at_line(doc, first_line);
  const std::string ending(line_ending_of(buf, first_line));

  std::string joined;
  for (const std::size_t l : *lines) joined += line_text(buf, l).text + "\n";
  std::string fence = code_fence_for(joined);
  if (fence.size() < 3) fence.assign(3, '`');

  const bool item_line = std::any_of(ctx.begin(), ctx.end(), [&](const Context& c) {
    return !c.quote && first_line_of(doc, c.node) == first_line;
  });
  std::size_t open_offset = first_lt.start;
  std::string open;
  if (item_line) {
    const std::size_t col = container_end_column(first_lt.text, ctx, doc, first_line);
    open_offset += col;
    open = pad_before(first_lt.text, col) + fence + ending;
    if (col < first_lt.text.size() || lone_blank) open += continuation_prefix(ctx);
  } else {
    open = continuation_prefix(ctx) + fence + ending;
    if (lone_blank) open += blank_line_pad(doc, first_line);  
  }
  const std::string close = ending + continuation_prefix(ctx) + fence;

  const auto shift = [&](std::size_t pos) {
    return pos + (pos >= open_offset ? open.size() : 0) + (pos > last_lt.end() ? close.size() : 0);
  };
  const Selection at = lone_blank ? Selection{first_lt.end(), first_lt.end()} : sel;
  BlockDraft draft{{Edit{open_offset, 0, open}, Edit{last_lt.end(), 0, close}}, sel, std::nullopt, std::nullopt,
                   Selection{shift(at.anchor), shift(at.cursor)}};
  if (lone_blank) draft.check_typed = true;
  return resolve_blanks(doc, std::move(draft), "toggle_block", "code_on");
}

std::optional<CommandResult> code_off(const Document& doc, Selection sel) {
  const SyntaxTree& tree = doc.tree();
  const Buffer& buf = doc.buffer();
  const NodeId cb =
      innermost_of_kind(doc, std::min(sel.anchor, sel.cursor), std::max(sel.anchor, sel.cursor), NodeKind::code_block);
  if (cb == no_node) return std::nullopt;
  const std::size_t first = first_line_of(doc, cb);
  const std::size_t last = last_line_of(doc, cb);
  const auto line_end = [&](std::size_t l) { return l + 1 < buf.line_count() ? buf.line_start(l + 1) : buf.size(); };

  std::vector<Edit> edits;
  if (tree[cb].fenced) {
    std::vector<NodeId> fences;
    for (const NodeId c : tree.children(cb)) {
      if (tree[c].kind == NodeKind::code_fence_delimiter) fences.push_back(c);
    }
    if (fences.empty()) return std::nullopt;
    const std::size_t open = tree[fences.front()].start;
    edits.push_back(Edit{open, line_end(buf.line_of(open)) - open, {}});
    if (fences.size() > 1) {
      const std::size_t close_line = buf.line_of(tree[fences.back()].start);
      std::size_t from = buf.line_start(close_line);
      if (close_line + 1 >= buf.line_count() && close_line > 0) from = line_text(buf, close_line - 1).end();
      edits.push_back(Edit{from, line_end(close_line) - from, {}});
    }
  } else {
    for (std::size_t l = first; l <= last; ++l) {
      const LineText line = line_text(buf, l);
      const std::size_t col = container_end_column(line.text, contexts_at_line(doc, l), doc, l);
      std::size_t n = 0;
      while (n < 4 && col + n < line.text.size() && is_space(line.text[col + n])) ++n;
      if (n > 0) edits.push_back(Edit{line.start + col, n, {}});
    }
  }
  const BlockDraft draft{std::move(edits), sel, blank_before_at(doc, first, contexts_at_line(doc, first)),
                         blank_after_at(doc, last, contexts_at_line(doc, last)), std::nullopt};
  return resolve_blanks(doc, draft, "toggle_block", "code_off");
}

}  

std::optional<CommandResult> toggle_block(const Document& doc, Selection sel, BlockKind kind) {
  Format fmt = format_at(doc, sel);
  const Buffer& buf = doc.buffer();
  if (buf.line_of(sel.anchor) == buf.line_of(sel.cursor) && all_space(line_text(buf, buf.line_of(sel.cursor)).text)) {
    fmt.list = ListKind::none;
    fmt.task = false;
    fmt.block_quote = false;
  }
  switch (kind) {
    case BlockKind::bullet_list: return fmt.list == ListKind::bullet ? list_off(doc, sel) : list_on(doc, sel, false);
    case BlockKind::ordered_list:
      return fmt.list == ListKind::ordered ? list_off(doc, sel) : list_on(doc, sel, true);
    case BlockKind::task_list:
      if (!doc.dialect().task_lists) return std::nullopt;
      return fmt.task ? task_off(doc, sel) : task_on(doc, sel);
    case BlockKind::block_quote: return fmt.block_quote ? quote_off(doc, sel) : quote_on(doc, sel);
    case BlockKind::code_block: return fmt.code_block ? code_off(doc, sel) : code_on(doc, sel);
  }
  return std::nullopt;
}

namespace {

std::optional<CommandResult> thematic_break_on_empty(const Document& doc, const BlockLine& bl) {
  const Buffer& buf = doc.buffer();
  const LineText& lt = bl.lt;
  const std::size_t line_no = buf.line_of(lt.start);
  if (line_in_kind(doc, line_no, NodeKind::code_block)) return std::nullopt;
  const std::vector<Context> ctx = empty_line_contexts(doc, line_no, lt);
  std::string first = lt.text.substr(0, std::min(bl.col, lt.text.size()));
  if (all_space(first)) {
    first = continuation_prefix(ctx);
  } else if (!is_space(first.back())) {
    first += ' ';  
  }
  std::string text = first + "---";
  std::size_t target = lt.start + text.size() + line_ending_of(buf, line_no).size();
  if (line_no + 1 >= buf.line_count()) {
    text += line_ending_of(buf, line_no > 0 ? line_no - 1 : line_no);
    target = lt.start + text.size();
  }
  auto r = place_block(doc, Edit{lt.start, lt.text.size(), std::move(text)}, ctx, lt.start + first.size(),
                       NodeKind::thematic_break, target, "insert_thematic_break", "empty_line");
  if (r) {
    std::string after = doc.text();
    apply_to(after, r->edits);
    const std::string cont = continuation_prefix(ctx);
    if (!cont.empty() && after.compare(r->selection.cursor, cont.size(), cont) == 0)
      r->selection.cursor = r->selection.anchor = r->selection.cursor + cont.size();
  }
  return r;
}

}  

std::optional<CommandResult> insert_thematic_break(const Document& doc, Selection sel) {
  const SyntaxTree& tree = doc.tree();
  const Buffer& buf = doc.buffer();
  const BlockLine bl = block_line(doc, buf.line_of(sel.cursor));
  if (bl.blank()) return thematic_break_on_empty(doc, bl);
  NodeId table = no_node;
  NodeId leaf = no_node;
  for (NodeId cur = node_before(doc, std::max(sel.anchor, sel.cursor)); cur != no_node; cur = tree[cur].parent) {
    const NodeKind k = tree[cur].kind;
    if (k == NodeKind::code_block) return std::nullopt;
    if (k == NodeKind::table) table = cur;
    if (leaf == no_node && (k == NodeKind::paragraph || k == NodeKind::heading || k == NodeKind::thematic_break ||
                            k == NodeKind::html_block || k == NodeKind::link_reference_definition))
      leaf = cur;
  }
  const NodeId block = table != no_node ? table : leaf;
  if (block == no_node) return std::nullopt;

  const std::size_t end_line = last_line_of(doc, block);
  const std::size_t end = line_text(buf, end_line).end();
  const std::vector<Context> ctx = contexts_at_line(doc, end_line);
  const std::string_view ending = line_ending_of(buf, end_line);
  std::string text(ending);
  text += quote_prefix(ctx);
  text += ending;
  text += continuation_prefix(ctx);
  text += "---";
  const NodeId next = tree[block].next_sibling;
  std::size_t cursor = buf.size() + text.size();
  if (next != no_node) {
    cursor = tree[next].start + text.size();
  } else if (end == buf.size()) {
    text += ending;
    cursor += ending.size();
  }
  log_decision("insert_thematic_break", next != no_node ? "next_block" : "end", 1);
  return checked_block(doc, at_cursor({Edit{end, 0, std::move(text)}}, cursor), "insert_thematic_break");
}

namespace {

struct LineRange {
  std::size_t start = 0;
  std::size_t end = 0;
};

LineRange line_range(const Buffer& buf, std::size_t from, std::size_t to) {
  const std::size_t first = buf.line_of(from);
  const std::size_t last = buf.line_of(to);
  return {buf.line_start(first), last + 1 < buf.line_count() ? buf.line_start(last + 1) : buf.size() + 1};
}

int list_depth(const SyntaxTree& tree, NodeId id) {
  int depth = 0;
  for (NodeId cur = id; cur != no_node; cur = tree[cur].parent) {
    if (tree[cur].kind == NodeKind::list) ++depth;
  }
  return depth;
}

std::vector<NodeId> items_in(const SyntaxTree& tree, LineRange r) {
  std::vector<NodeId> out;
  const auto nodes = tree.nodes();
  auto it = std::lower_bound(nodes.begin(), nodes.end(), r.start,
                             [](const Node& n, std::size_t v) { return n.start < v; });
  for (; it != nodes.end() && it->start < r.end; ++it) {
    if (it->kind == NodeKind::list_item) out.push_back(static_cast<NodeId>(it - nodes.begin()));
  }
  return out;
}

std::vector<NodeId> ordered_lists_at(const SyntaxTree& tree, LineRange r, const std::vector<NodeId>& items) {
  std::vector<NodeId> out;
  const auto add = [&](NodeId list) {
    if (tree[list].ordered && std::find(out.begin(), out.end(), list) == out.end()) out.push_back(list);
  };
  for (NodeId cur = tree.node_at(r.start); cur != no_node; cur = tree[cur].parent) {
    if (tree[cur].kind == NodeKind::list && tree[cur].start < r.start) add(cur);
  }
  for (NodeId item : items) add(tree[item].parent);
  return out;
}

std::vector<Context> item_contexts(const Document& doc, NodeId list) {
  std::vector<Context> out;
  for (NodeId child : doc.tree().children(list)) {
    if (doc.tree()[child].kind != NodeKind::list_item) continue;  
    if (auto c = make_context(doc, child)) out.push_back(std::move(*c));
  }
  return out;
}

bool marker_edited(const SyntaxTree& tree, NodeId item, std::size_t from, std::size_t to) {
  for (NodeId child : tree.children(item)) {
    if (tree[child].kind != NodeKind::list_marker) continue;
    const Node& m = tree[child];
    return to > from ? from < m.end && to > m.start : m.start <= from && from <= m.end;
  }
  return false;
}

void renumber_list(const Document& doc, NodeId list, LineRange r, const std::vector<NodeId>& fresh,
                   std::size_t from, std::size_t to, bool same_shape, const ListShape& before,
                   std::vector<Edit>& edits) {
  constexpr std::uint64_t max_number = 999'999'999;  
  const SyntaxTree& tree = doc.tree();
  const std::vector<Context> items = item_contexts(doc, list);
  const auto is_new = [&](const Context& c) { return std::find(fresh.begin(), fresh.end(), c.node) != fresh.end(); };
  std::size_t pivot = 0;
  while (pivot < items.size() && tree[items[pivot].node].start < r.start) ++pivot;
  if (pivot == items.size()) return;

  std::size_t outside = 0;
  bool one = true;
  std::uint64_t same = 0;
  for (const Context& c : items) {
    if (is_new(c)) continue;
    if (outside++ == 0) {
      same = c.number;
    } else if (c.number != same) {
      one = false;
    }
  }
  const bool one_number = one && outside >= 2;

  std::uint64_t next = items[pivot].number;
  if (one_number) {
    next = same;
  } else if (pivot > 0) {
    next = items[pivot - 1].number + 1;
  } else {
    const int depth = list_depth(tree, list);
    for (const auto& [d, n] : before.first_numbers) {
      if (d == depth && next != 1) {
        next = n;
        break;
      }
    }
  }
  std::optional<std::uint64_t> last_old;
  for (std::size_t i = pivot; i < items.size() && next <= max_number; ++i) {
    const Context& c = items[i];
    if (!is_new(c)) {
      if (last_old && c.number != *last_old + 1) return;
      last_old = c.number;
    }
    if (i == 0 && same_shape && marker_edited(tree, c.node, from, to)) {
      next = c.number;
    } else if (c.number != next) {
      replace_marker(doc, c, num(next) + c.bullet, edits);
    }
    if (!one_number) ++next;
  }
}

}  

ListShape list_shape(const Document& doc, std::size_t from, std::size_t to) {
  const SyntaxTree& tree = doc.tree();
  const LineRange r = line_range(doc.buffer(), from, to);
  const std::vector<NodeId> items = items_in(tree, r);
  ListShape s;
  for (NodeId item : items) s.items.push_back({list_depth(tree, item), tree[tree[item].parent].ordered});
  for (NodeId list : ordered_lists_at(tree, r, items)) {
    const int depth = list_depth(tree, list);
    const auto known = std::find_if(s.first_numbers.begin(), s.first_numbers.end(),
                                    [&](const auto& p) { return p.first == depth; });
    if (known != s.first_numbers.end()) continue;
    for (NodeId child : tree.children(list)) {
      if (tree[child].kind != NodeKind::list_item) continue;
      if (const auto c = make_context(doc, child)) s.first_numbers.emplace_back(depth, c->number);
      break;
    }
  }
  return s;
}

std::optional<CommandResult> renumber_lists(const Document& doc, const ListShape& before, std::size_t from,
                                            std::size_t to, Selection sel) {
  const SyntaxTree& tree = doc.tree();
  const LineRange r = line_range(doc.buffer(), from, to);
  const std::vector<NodeId> items = items_in(tree, r);
  std::vector<ListShape::Item> now;
  for (NodeId item : items) now.push_back({list_depth(tree, item), tree[tree[item].parent].ordered});
  const bool digits = std::any_of(items.begin(), items.end(), [&](NodeId item) {
    return tree[tree[item].parent].ordered && marker_edited(tree, item, from, to);
  });
  const bool same_shape = now == before.items;
  if (same_shape && !digits) return std::nullopt;
  const Buffer& buf = doc.buffer();
  std::vector<NodeId> fresh;
  for (std::size_t i = 0; i < items.size(); ++i) {
    const std::size_t ls = buf.line_start(buf.line_of(tree[items[i]].start));
    std::size_t mark_end = tree[items[i]].start;
    for (NodeId child : tree.children(items[i])) {
      if (tree[child].kind == NodeKind::list_marker) mark_end = tree[child].end;
    }
    const bool edited = to > from ? from < mark_end && to > ls : ls < from && from <= mark_end;
    const bool moved = now.size() == before.items.size() && now[i] != before.items[i];
    if (edited || moved) fresh.push_back(items[i]);
  }
  std::vector<Edit> edits;
  for (NodeId list : ordered_lists_at(tree, r, items)) renumber_list(doc, list, r, fresh, from, to, same_shape, before, edits);
  if (edits.empty()) return std::nullopt;
  std::stable_sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.offset < b.offset; });
  log_decision("renumber", "lists", edits.size());
  return joined(doc, std::move(edits), sel);
}

std::vector<ByteRange> out_of_sequence(const Document& doc) {
  const SyntaxTree& tree = doc.tree();
  const Buffer& buf = doc.buffer();
  std::vector<ByteRange> out;
  const auto nodes = tree.nodes();
  struct Number {
    ByteRange digits;
    std::uint64_t value = 0;
  };
  std::vector<Number> items;
  for (std::size_t id = 0; id < nodes.size(); ++id) {
    if (nodes[id].kind != NodeKind::list || !nodes[id].ordered) continue;
    items.clear();
    for (NodeId item : tree.children(static_cast<NodeId>(id))) {
      if (tree[item].kind != NodeKind::list_item) continue;
      for (NodeId child : tree.children(item)) {
        if (tree[child].kind != NodeKind::list_marker) continue;
        Number n{{tree[child].start, tree[child].end - 1}, 0};
        for (std::size_t p = n.digits.start; p < n.digits.end; ++p) {
          n.value = n.value * 10 + static_cast<std::uint64_t>(buf.at(p) - '0');
        }
        items.push_back(n);
        break;
      }
    }
    const bool one_number = items.size() >= 2 && std::all_of(items.begin(), items.end(), [&](const Number& n) {
                              return n.value == items.front().value;
                            });
    if (one_number) continue;
    for (std::size_t i = 1; i < items.size(); ++i) {
      if (items[i].value != items[i - 1].value + 1) out.push_back(items[i].digits);
    }
  }
  std::sort(out.begin(), out.end(), [](const ByteRange& a, const ByteRange& b) { return a.start < b.start; });
  return out;
}

std::optional<CommandResult> renumber(const Document& doc, Selection sel) {
  constexpr std::uint64_t max_number = 999'999'999;  
  const SyntaxTree& tree = doc.tree();
  const Buffer& buf = doc.buffer();
  const std::size_t lo = std::min(sel.anchor, sel.cursor);
  const std::size_t hi = std::max(sel.anchor, sel.cursor);
  std::size_t first_line = buf.line_of(lo);
  std::size_t last_line = buf.line_of(hi);
  if (hi > lo && last_line > first_line && buf.line_start(last_line) == hi) --last_line;
  const LineRange r{buf.line_start(first_line),
                    last_line + 1 < buf.line_count() ? buf.line_start(last_line + 1) : buf.size() + 1};
  std::vector<NodeId> lists;
  for (NodeId item : items_in(tree, r)) {
    const NodeId list = tree[item].parent;
    if (tree[list].ordered && std::find(lists.begin(), lists.end(), list) == lists.end()) lists.push_back(list);
  }
  for (NodeId cur = tree.node_at(lo); cur != no_node; cur = tree[cur].parent) {
    if (tree[cur].kind == NodeKind::list && tree[cur].ordered) {
      if (std::find(lists.begin(), lists.end(), cur) == lists.end()) lists.push_back(cur);
      break;
    }
  }
  if (lists.empty()) return std::nullopt;
  std::vector<Edit> edits;
  for (NodeId list : lists) {
    const std::vector<Context> items = item_contexts(doc, list);
    if (items.empty()) continue;
    std::uint64_t next = items.front().number;
    for (const Context& c : items) {
      if (next > max_number) break;
      if (c.number != next) replace_marker(doc, c, num(next) + c.bullet, edits);
      ++next;
    }
  }
  std::stable_sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.offset < b.offset; });
  log_decision("renumber", "selection", edits.size());
  return joined(doc, std::move(edits), sel);
}

std::optional<CommandResult> marker_to_text(const Document& doc, Selection sel) {
  const SyntaxTree& tree = doc.tree();
  for (NodeId cur = tree.node_at(sel.cursor); cur != no_node; cur = tree[cur].parent) {
    if (tree[cur].kind != NodeKind::list_item) continue;
    if (!tree[tree[cur].parent].ordered) return std::nullopt;
    const auto c = make_context(doc, cur);
    if (!c) return std::nullopt;
    const std::size_t at = doc.buffer().line_start(first_line_of(doc, cur)) + c->mark_end - c->bullet.size();
    log_decision("marker_to_text", "item", 1);
    return from_ascending({Edit{at, 0, "\\"}}, sel);
  }
  return std::nullopt;
}

std::optional<CommandResult> drop_text(const Document& doc, ByteRange source, std::size_t target, bool move) {
  const std::size_t len = source.end - source.start;
  if (move && target >= source.start && target <= source.end) return std::nullopt;
  const std::string bytes = doc.buffer().slice(source.start, len);
  if (!move) {
    log_decision("drop_text", "copy", 1);
    return CommandResult{{Edit{target, 0, bytes}}, {target, target + len}};
  }
  CommandResult r;
  if (target < source.start) {
    r.edits = {Edit{target, 0, bytes}, Edit{source.start + len, len, ""}};
    r.selection = {target, target + len};
  } else {
    r.edits = {Edit{source.start, len, ""}, Edit{target - len, 0, bytes}};
    r.selection = {target - len, target};
  }
  log_decision("drop_text", "move", r.edits.size());
  return r;
}

}  
