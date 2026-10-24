#include "lumos/commands.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "commands_detail.hpp"

namespace lumos {
using namespace detail;
namespace {

std::string trim_cell(std::string_view s) {
  std::size_t a = 0;
  std::size_t b = s.size();
  while (a < b && is_space(s[a])) ++a;
  while (b > a && is_space(s[b - 1])) --b;
  return std::string(s.substr(a, b - a));
}

std::size_t codepoint_len(std::string_view s) {
  std::size_t n = 0;
  for (char c : s) {
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
  }
  return n;
}

std::string existing_ending(const Buffer& buf, std::size_t line) {
  if (line + 1 >= buf.line_count()) return {};
  return std::string(line_ending_of(buf, line));
}

struct TableRow {
  std::size_t line = 0;
  std::string prefix;
  std::vector<std::string> cells;
  std::vector<std::pair<std::size_t, std::size_t>> cell_ranges;
  std::string ending;  
  std::vector<std::size_t> pipes;  
  std::size_t content_start = 0;  
};

TableRow extract_row(const Document& doc, NodeId row_id) {
  const SyntaxTree& tree = doc.tree();
  const Buffer& buf = doc.buffer();
  const Node& row = tree[row_id];
  const std::size_t line_no = buf.line_of(row.start);
  const LineText lt = line_text(buf, line_no);
  std::size_t content_start = row.start;
  while (content_start < lt.end() && is_space(buf.at(content_start))) ++content_start;

  TableRow out;
  out.line = line_no;
  out.prefix = lt.text.substr(0, content_start - lt.start);
  out.ending = existing_ending(buf, line_no);
  out.content_start = content_start;
  for (NodeId child : tree.children(row_id)) {
    if (tree[child].kind == NodeKind::table_pipe) out.pipes.push_back(tree[child].start);
  }
  const bool leading = !out.pipes.empty() && out.pipes.front() == content_start;
  const bool trailing = !out.pipes.empty() && out.pipes.back() + 1 == lt.end();
  std::vector<std::pair<std::size_t, std::size_t>> bounds;
  std::size_t cur = content_start;
  for (std::size_t p : out.pipes) {
    bounds.push_back({cur, p});
    cur = p + 1;
  }
  bounds.push_back({cur, lt.end()});
  if (leading) bounds.erase(bounds.begin());
  if (trailing) bounds.pop_back();
  for (const auto& [s, e] : bounds) {
    out.cells.push_back(trim_cell(buf.slice(s, e - s)));
    out.cell_ranges.push_back({s, e});
  }
  return out;
}

std::size_t column_at(const TableRow& row, std::size_t header_count, std::size_t cursor) {
  std::size_t count = 0;
  for (std::size_t p : row.pipes) {
    if (p < cursor) ++count;
  }
  if (!row.pipes.empty() && row.pipes.front() == row.content_start && count > 0) --count;
  return header_count == 0 ? 0 : std::min(count, header_count - 1);
}

struct TableModel {
  std::size_t header_count = 0;
  std::vector<Alignment> aligns;  
  TableRow header;
  TableRow delimiter;  
  std::vector<TableRow> body;
};

std::optional<TableModel> build_model(const Document& doc, NodeId table_id) {
  const SyntaxTree& tree = doc.tree();
  NodeId header_id = no_node;
  NodeId delim_id = no_node;
  std::vector<NodeId> body_ids;
  for (NodeId child : tree.children(table_id)) {
    switch (tree[child].kind) {
      case NodeKind::table_header_row: header_id = child; break;
      case NodeKind::table_delimiter_row: delim_id = child; break;
      case NodeKind::table_row: body_ids.push_back(child); break;
      default: break;
    }
  }
  if (header_id == no_node || delim_id == no_node) return std::nullopt;
  TableModel m;
  m.header = extract_row(doc, header_id);
  m.delimiter = extract_row(doc, delim_id);
  for (NodeId b : body_ids) m.body.push_back(extract_row(doc, b));
  m.header_count = m.header.cells.size();
  if (m.header_count == 0) return std::nullopt;
  m.aligns.assign(m.header_count, Alignment::none);
  std::size_t idx = 0;
  for (NodeId child : tree.children(delim_id)) {
    if (tree[child].kind == NodeKind::table_delimiter_cell) {
      if (idx < m.header_count) m.aligns[idx] = tree[child].align;
      ++idx;
    }
  }
  return m;
}

std::size_t total_columns(const TableModel& m) {
  std::size_t n = m.header_count;
  for (const TableRow& r : m.body) n = std::max(n, r.cells.size());
  return n;
}

std::vector<std::size_t> compute_widths(const TableModel& m) {
  std::vector<std::size_t> w(total_columns(m), 3);
  auto consider = [&](const TableRow& r) {
    for (std::size_t j = 0; j < r.cells.size(); ++j) w[j] = std::max(w[j], codepoint_len(r.cells[j]));
  };
  consider(m.header);
  for (const TableRow& r : m.body) consider(r);
  return w;
}

struct CellSpan {
  std::size_t start = 0;
  std::size_t end = 0;
};

struct Rendered {
  std::string text;
  std::vector<std::vector<CellSpan>> ranges;
};

void append_padded(std::string& out, std::string_view cell, std::size_t width, Alignment align, CellSpan& span) {
  const std::size_t len = codepoint_len(cell);
  const std::size_t pad = width > len ? width - len : 0;
  std::size_t before = 0;
  std::size_t after = 0;
  switch (align) {
    case Alignment::right: before = pad; break;
    case Alignment::center: before = pad / 2; after = pad - before; break;
    default: after = pad; break;  
  }
  out.append(before, ' ');
  span.start = out.size();
  out += cell;
  span.end = out.size();
  out.append(after, ' ');
}

void write_row(std::string& out, const TableRow& row, const std::vector<std::size_t>& widths,
              const std::vector<Alignment>& aligns, std::vector<CellSpan>& spans) {
  const std::size_t len = std::max(aligns.size(), row.cells.size());
  out += row.prefix;
  out += "| ";
  for (std::size_t j = 0; j < len; ++j) {
    if (j > 0) out += " | ";
    const std::string_view cell = j < row.cells.size() ? std::string_view(row.cells[j]) : std::string_view();
    const Alignment a = j < aligns.size() ? aligns[j] : Alignment::none;
    CellSpan span;
    append_padded(out, cell, widths[j], a, span);
    spans.push_back(span);
  }
  out += " |";
  out += row.ending;
}

void write_delimiter(std::string& out, const TableRow& delim, const std::vector<std::size_t>& widths,
                     const std::vector<Alignment>& aligns) {
  out += delim.prefix;
  out += "| ";
  for (std::size_t j = 0; j < aligns.size(); ++j) {
    if (j > 0) out += " | ";
    const bool lc = aligns[j] == Alignment::left || aligns[j] == Alignment::center;
    const bool rc = aligns[j] == Alignment::right || aligns[j] == Alignment::center;
    const std::size_t dashes = widths[j] - (lc ? 1 : 0) - (rc ? 1 : 0);
    if (lc) out += ':';
    out.append(dashes, '-');
    if (rc) out += ':';
  }
  out += " |";
  out += delim.ending;
}

Rendered render(const TableModel& m) {
  Rendered out;
  const std::vector<std::size_t> widths = compute_widths(m);
  std::vector<CellSpan> spans;
  write_row(out.text, m.header, widths, m.aligns, spans);
  out.ranges.push_back(std::move(spans));
  write_delimiter(out.text, m.delimiter, widths, m.aligns);
  for (const TableRow& r : m.body) {
    std::vector<CellSpan> rs;
    write_row(out.text, r, widths, m.aligns, rs);
    out.ranges.push_back(std::move(rs));
  }
  return out;
}

std::optional<Edit> diff_edit(std::size_t base, const std::string& old_text, const std::string& new_text) {
  if (old_text == new_text) return std::nullopt;
  const std::size_t max_common = std::min(old_text.size(), new_text.size());
  std::size_t prefix = 0;
  while (prefix < max_common && old_text[prefix] == new_text[prefix]) ++prefix;
  std::size_t suffix = 0;
  const std::size_t max_suffix = max_common - prefix;
  while (suffix < max_suffix && old_text[old_text.size() - 1 - suffix] == new_text[new_text.size() - 1 - suffix]) {
    ++suffix;
  }
  return Edit{base + prefix, old_text.size() - prefix - suffix,
             new_text.substr(prefix, new_text.size() - prefix - suffix)};
}

struct AlignOutcome {
  std::vector<Edit> edits;
  bool aligned = false;
  Rendered rendered;
};

AlignOutcome align_table(const Document& doc, std::size_t base, std::size_t node_start, std::size_t table_end,
                         const TableModel& model) {
  AlignOutcome out;
  Rendered r = render(model);
  const std::string old_text = doc.buffer().slice(base, table_end - base);
  const std::optional<Edit> edit = diff_edit(base, old_text, r.text);
  if (!edit) {
    out.aligned = true;
    out.rendered = std::move(r);
    return out;
  }
  std::string after = doc.text();
  after.replace(edit->offset, edit->removed, edit->inserted);
  const Document check(std::move(after), doc.dialect());
  const SyntaxTree& ctree = check.tree();
  NodeId t = ctree.node_at(node_start);
  while (t != no_node && ctree[t].kind != NodeKind::table) t = ctree[t].parent;
  bool ok = false;
  auto expand = [&](const TableRow& row) {
    std::vector<std::string> cells = row.cells;
    if (cells.size() < model.header_count) cells.resize(model.header_count);
    return cells;
  };
  if (t != no_node && ctree[t].start == node_start) {
    if (const auto nm = build_model(check, t)) {
      ok = nm->header.cells == expand(model.header) && nm->body.size() == model.body.size();
      for (std::size_t i = 0; ok && i < model.body.size(); ++i) ok = nm->body[i].cells == expand(model.body[i]);
    }
  }
  if (ok) {
    out.edits = {*edit};
    out.aligned = true;
    out.rendered = std::move(r);
  } else {
    log_decision("table", "align_failed", 0);
  }
  return out;
}

std::size_t phys_count(const TableModel& m) { return 2 + m.body.size(); }

const TableRow& row_at(const TableModel& m, std::size_t phys) {
  if (phys == 0) return m.header;
  if (phys == 1) return m.delimiter;
  return m.body[phys - 2];
}

std::size_t ranges_index(std::size_t phys) { return phys == 0 ? 0 : phys - 1; }

std::optional<std::size_t> advance(const TableModel& m, std::size_t phys) {
  std::size_t r = phys + 1;
  if (r == 1) r = 2;
  if (r >= phys_count(m)) return std::nullopt;
  return r;
}

std::optional<std::size_t> retreat(std::size_t phys) {
  if (phys == 0) return std::nullopt;
  std::size_t r = phys - 1;
  if (r == 1) r = 0;
  return r;
}

bool continues_same_containers(const Document& doc, std::size_t line, const std::vector<Context>& outer) {
  const std::vector<Context> ctx = contexts_at_line(doc, line);
  if (ctx.size() != outer.size()) return false;
  for (std::size_t i = 0; i < ctx.size(); ++i) {
    if (ctx[i].node != outer[i].node) return false;
  }
  return true;
}

bool line_is_blank(const Document& doc, std::size_t line) {
  const Buffer& buf = doc.buffer();
  if (line >= buf.line_count()) return true;
  const LineText lt = line_text(buf, line);
  for (char c : lt.text) {
    if (c != ' ' && c != '\t' && c != '>') return false;
  }
  return true;
}

struct Located {
  NodeId table = no_node;
  TableModel model;
  std::size_t phys = 0;
  std::size_t col = 0;
};

std::optional<Located> locate(const Document& doc, std::size_t cursor) {
  const SyntaxTree& tree = doc.tree();
  const Buffer& buf = doc.buffer();
  const std::size_t line_no = buf.line_of(cursor);
  NodeId table = no_node;
  for (NodeId cur = node_before(doc, cursor); cur != no_node; cur = tree[cur].parent) {
    if (tree[cur].kind == NodeKind::table && line_no <= last_line_of(doc, cur)) {
      table = cur;
      break;
    }
  }
  if (table == no_node) return std::nullopt;
  auto model = build_model(doc, table);
  if (!model) return std::nullopt;
  std::size_t phys = phys_count(*model);  
  if (model->header.line == line_no) {
    phys = 0;
  } else if (model->delimiter.line == line_no) {
    phys = 1;
  } else {
    for (std::size_t i = 0; i < model->body.size(); ++i) {
      if (model->body[i].line == line_no) {
        phys = 2 + i;
        break;
      }
    }
  }
  if (phys >= phys_count(*model)) return std::nullopt;
  const std::size_t col = column_at(row_at(*model, phys), model->header_count, cursor);
  return Located{table, std::move(*model), phys, col};
}

CommandResult format_core(const Document& doc, Selection sel, NodeId table, const TableModel& model,
                          std::string_view event, std::string_view kind) {
  const std::size_t base = doc.buffer().line_start(model.header.line);
  const std::size_t node_start = doc.tree()[table].start;
  const std::size_t table_end = doc.tree()[table].end;
  AlignOutcome outcome = align_table(doc, base, node_start, table_end, model);
  CommandResult r;
  if (outcome.aligned) r.edits = std::move(outcome.edits);
  r.selection = {map_position(sel.anchor, r.edits), map_position(sel.cursor, r.edits)};
  log_decision(event, kind, r.edits.size());
  return r;
}

struct AddInfo {
  TableRow old_last;
  bool no_ending = false;
};

CommandResult finish(const Document& doc, NodeId table, const TableModel& model, std::size_t target_phys,
                     std::size_t target_col, const std::optional<AddInfo>& add, std::string_view newline,
                     std::string_view event, std::string_view kind) {
  const std::size_t base = doc.buffer().line_start(model.header.line);
  const std::size_t node_start = doc.tree()[table].start;
  const std::size_t table_end = doc.tree()[table].end;
  AlignOutcome outcome = align_table(doc, base, node_start, table_end, model);
  CommandResult r;
  std::size_t abs_start = 0;
  std::size_t abs_end = 0;
  if (outcome.aligned) {
    r.edits = std::move(outcome.edits);
    const CellSpan& span = outcome.rendered.ranges[ranges_index(target_phys)][target_col];
    abs_start = base + span.start;
    abs_end = base + span.end;
  } else if (add) {
    std::string text;
    if (add->no_ending) text += std::string(newline);
    text += add->old_last.prefix;
    text += "|";
    const std::size_t cell_pos = text.size();
    text += std::string(model.header_count, '|');
    text += model.header.ending;
    r.edits = {Edit{table_end, 0, std::move(text)}};
    abs_start = abs_end = table_end + cell_pos;
  } else {
    const TableRow& row = row_at(model, target_phys);
    std::tie(abs_start, abs_end) = row.cell_ranges[target_col];
  }
  r.selection = {abs_start, abs_end};
  log_decision(event, kind, r.edits.size());
  return r;
}

CommandResult leave_table(const Document& doc, NodeId table, const TableModel& model, std::string_view newline) {
  const std::size_t base = doc.buffer().line_start(model.header.line);
  const std::size_t node_start = doc.tree()[table].start;
  const std::size_t table_end = doc.tree()[table].end;
  const AlignOutcome outcome = align_table(doc, base, node_start, table_end, model);
  const std::optional<Edit> align_edit =
      outcome.aligned && !outcome.edits.empty() ? std::optional<Edit>(outcome.edits.front()) : std::nullopt;

  const Buffer& buf = doc.buffer();
  const std::size_t last_line = last_line_of(doc, table);
  const std::vector<Context> ctx = contexts_at_line(doc, last_line);
  const bool at_doc_end = last_line + 1 >= buf.line_count();

  std::vector<Edit> prior;
  if (align_edit) prior.push_back(*align_edit);
  std::size_t final_cursor = 0;
  std::vector<Edit> pieces;  

  auto emit = [&](std::size_t off, std::string txt) {
    if (!pieces.empty() && pieces.back().offset == off) {
      pieces.back().inserted += txt;
    } else {
      pieces.push_back(Edit{off, 0, std::move(txt)});
    }
  };

  if (at_doc_end) {
    const std::string pre = std::string(newline) + quote_prefix(ctx) + std::string(newline);
    const std::string cursor_prefix = continuation_prefix(ctx);
    final_cursor = map_position(table_end, prior) + pre.size() + cursor_prefix.size();
    emit(table_end, pre + cursor_prefix);
  } else {
    const std::size_t l0 = last_line + 1;
    const bool sep_reuse = continues_same_containers(doc, l0, ctx) && line_is_blank(doc, l0);
    const std::size_t cursor_candidate = sep_reuse ? l0 + 1 : l0;
    const bool cursor_reuse = cursor_candidate < buf.line_count() &&
                              continues_same_containers(doc, cursor_candidate, ctx) &&
                              line_is_blank(doc, cursor_candidate);
    const std::size_t next_after_cursor = cursor_reuse ? cursor_candidate + 1 : cursor_candidate;
    const bool need_trailing = next_after_cursor < buf.line_count() &&
                               !(continues_same_containers(doc, next_after_cursor, ctx) &&
                                 line_is_blank(doc, next_after_cursor));

    if (!sep_reuse) emit(buf.line_start(l0), quote_prefix(ctx) + std::string(newline));
    if (cursor_reuse) {
      final_cursor = map_position(line_text(buf, cursor_candidate).end(), prior);
    } else {
      const bool append_at_end = cursor_candidate >= buf.line_count();
      const std::size_t off = append_at_end ? buf.size() : buf.line_start(cursor_candidate);
      const std::string prefix = continuation_prefix(ctx);
      const std::string lead = append_at_end ? std::string(newline) : std::string();
      const std::size_t already =
          !pieces.empty() && pieces.back().offset == off ? pieces.back().inserted.size() : 0;
      final_cursor = map_position(off, prior) + already + lead.size() + prefix.size();
      emit(off, lead + prefix + (append_at_end ? std::string() : std::string(newline)));
    }
    if (need_trailing) emit(buf.line_start(next_after_cursor), quote_prefix(ctx) + std::string(newline));
  }

  std::vector<Edit> ascending;
  if (align_edit) ascending.push_back(*align_edit);
  for (Edit& e : pieces) ascending.push_back(std::move(e));

  CommandResult r;
  r.selection = {final_cursor, final_cursor};
  std::reverse(ascending.begin(), ascending.end());
  r.edits = std::move(ascending);
  log_decision("next_row", "leave", r.edits.size());
  return r;
}

TableModel build_insert_model(std::size_t columns, std::size_t body_count, std::string prefix,
                              const std::string& cont_prefix, const std::string& sep,
                              const std::string& last_ending) {
  TableModel m;
  m.header_count = columns;
  m.aligns.assign(columns, Alignment::none);
  m.header.cells.assign(columns, std::string());
  m.header.prefix = std::move(prefix);
  m.header.ending = sep;
  m.delimiter.prefix = cont_prefix;
  m.delimiter.ending = body_count == 0 ? last_ending : sep;
  m.body.resize(body_count);
  for (std::size_t i = 0; i < body_count; ++i) {
    m.body[i].cells.assign(columns, std::string());
    m.body[i].prefix = cont_prefix;
    m.body[i].ending = (i + 1 == body_count) ? last_ending : sep;
  }
  return m;
}

std::optional<CommandResult> insert_table_on_empty(const Document& doc, const BlockLine& bl, std::size_t columns,
                                                   std::size_t body_count, std::string_view newline) {
  const Buffer& buf = doc.buffer();
  const LineText& lt = bl.lt;
  const std::size_t line_no = buf.line_of(lt.start);
  const std::vector<Context> ctx = empty_line_contexts(doc, line_no, lt);
  const std::string cont = continuation_prefix(ctx);
  std::string first = lt.text.substr(0, std::min(bl.col, lt.text.size()));
  if (all_space(first)) {
    first = cont;
  } else if (!is_space(first.back())) {
    first += ' ';  
  }
  const std::string real_ending = existing_ending(buf, line_no);
  const std::string sep = real_ending.empty() ? std::string(newline) : real_ending;
  const TableModel m = build_insert_model(columns, body_count, first, cont, sep, std::string());
  const Rendered r = render(m);
  const std::size_t target = lt.start + r.ranges[0][0].start;
  return place_block(doc, Edit{lt.start, lt.text.size(), r.text}, ctx, lt.start + first.size(), NodeKind::table, target,
                     "insert_table", "empty_line");
}

std::optional<CommandResult> insert_table_after(const Document& doc, NodeId block, std::size_t columns,
                                                std::size_t body_count, std::string_view newline) {
  const Buffer& buf = doc.buffer();
  const std::size_t end_line = last_line_of(doc, block);
  const std::size_t end = line_text(buf, end_line).end();
  const bool at_doc_end = end == buf.size();
  const std::string ending =
      end_line + 1 < buf.line_count() ? std::string(line_ending_of(buf, end_line)) : std::string(newline);
  const std::string cont = continuation_prefix(contexts_at_line(doc, end_line));
  const TableModel m =
      build_insert_model(columns, body_count, cont, cont, ending, at_doc_end ? ending : std::string());
  const Rendered r = render(m);
  const CellSpan& first_cell = r.ranges[0][0];
  const std::size_t row_start = end + ending.size();  
  const std::size_t node_start = row_start + cont.size();  
  const std::size_t target = row_start + first_cell.start;

  return place_block(doc, Edit{end, 0, ending + r.text}, contexts_at_line(doc, end_line), node_start,
                     NodeKind::table, target, "insert_table", "after_block");
}

}  

std::optional<CommandResult> next_cell(const Document& doc, Selection sel, std::string_view newline) {
  const auto loc = locate(doc, sel.cursor);
  if (!loc) return std::nullopt;
  TableModel model = loc->model;
  const std::size_t header_count = model.header_count;
  const std::size_t col = loc->col + 1;
  if (col < header_count) {
    return finish(doc, loc->table, model, loc->phys, col, std::nullopt, newline, "next_cell", "cell");
  }
  if (const auto nxt = advance(model, loc->phys)) {
    return finish(doc, loc->table, model, *nxt, 0, std::nullopt, newline, "next_cell", "cell");
  }
  TableRow& old_last = model.body.empty() ? model.delimiter : model.body.back();
  AddInfo info{old_last, old_last.ending.empty()};
  if (info.no_ending) old_last.ending = std::string(newline);
  TableRow new_row;
  new_row.prefix = info.old_last.prefix;
  new_row.cells.assign(header_count, std::string());
  new_row.ending = model.header.ending;
  model.body.push_back(std::move(new_row));
  const std::size_t target_phys = 2 + model.body.size() - 1;
  return finish(doc, loc->table, model, target_phys, 0, info, newline, "next_cell", "add_row");
}

std::optional<CommandResult> previous_cell(const Document& doc, Selection sel) {
  const auto loc = locate(doc, sel.cursor);
  if (!loc) return std::nullopt;
  if (loc->col > 0) {
    return finish(doc, loc->table, loc->model, loc->phys, loc->col - 1, std::nullopt, "", "previous_cell", "cell");
  }
  if (const auto prev = retreat(loc->phys)) {
    const std::size_t target_col = loc->model.header_count - 1;
    return finish(doc, loc->table, loc->model, *prev, target_col, std::nullopt, "", "previous_cell", "cell");
  }
  return format_core(doc, sel, loc->table, loc->model, "previous_cell", "header");
}

std::optional<CommandResult> next_row(const Document& doc, Selection sel, std::string_view newline) {
  const auto loc = locate(doc, sel.cursor);
  if (!loc) return std::nullopt;
  if (const auto nxt = advance(loc->model, loc->phys)) {
    return finish(doc, loc->table, loc->model, *nxt, loc->col, std::nullopt, newline, "next_row", "cell");
  }
  return leave_table(doc, loc->table, loc->model, newline);
}

std::optional<CommandResult> previous_row(const Document& doc, Selection sel) {
  const auto loc = locate(doc, sel.cursor);
  if (!loc) return std::nullopt;
  if (const auto prev = retreat(loc->phys)) {
    return finish(doc, loc->table, loc->model, *prev, loc->col, std::nullopt, "", "previous_row", "cell");
  }
  return format_core(doc, sel, loc->table, loc->model, "previous_row", "header");
}

std::optional<CommandResult> format_table(const Document& doc, Selection sel) {
  const auto loc = locate(doc, sel.cursor);
  if (!loc) return std::nullopt;
  return format_core(doc, sel, loc->table, loc->model, "format_table", "align");
}

std::optional<CommandResult> insert_table(const Document& doc, Selection sel, int rows, int columns,
                                          std::string_view newline) {
  if (rows < 1 || columns < 1) return std::nullopt;
  const std::size_t line_no = doc.buffer().line_of(sel.cursor);
  if (line_in_kind(doc, line_no, NodeKind::code_block) || line_in_kind(doc, line_no, NodeKind::table)) {
    return std::nullopt;
  }
  const auto columns_n = static_cast<std::size_t>(columns);
  const auto body_count = static_cast<std::size_t>(rows) - 1;
  const BlockLine bl = block_line(doc, line_no);
  if (bl.blank()) return insert_table_on_empty(doc, bl, columns_n, body_count, newline);

  const SyntaxTree& tree = doc.tree();
  const std::size_t end_pos = std::max(sel.anchor, sel.cursor);
  NodeId table_found = no_node;
  NodeId leaf = no_node;
  for (NodeId cur = node_before(doc, end_pos); cur != no_node; cur = tree[cur].parent) {
    const NodeKind k = tree[cur].kind;
    if (k == NodeKind::code_block) return std::nullopt;
    if (k == NodeKind::table) table_found = cur;
    if (leaf == no_node && (k == NodeKind::paragraph || k == NodeKind::heading || k == NodeKind::thematic_break ||
                            k == NodeKind::html_block || k == NodeKind::link_reference_definition)) {
      leaf = cur;
    }
  }
  const NodeId block = table_found != no_node ? table_found : leaf;
  if (block == no_node) return std::nullopt;
  return insert_table_after(doc, block, columns_n, body_count, newline);
}

}  
