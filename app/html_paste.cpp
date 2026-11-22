#include "html_paste.hpp"

#include <QChar>
#include <QFont>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTextList>
#include <QTextTable>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "lumos/log.hpp"
#include "lumos/md/html_renderer.hpp"
#include "lumos/syntax.hpp"

namespace {

constexpr std::string_view kComp = "app";

QString clean_text(const QString& in) {
  QString out;
  out.reserve(in.size());
  for (const QChar c : in) {
    if (c == QChar(0x00AD) || c == QChar(0x200B) || c == QChar(0xFEFF)) continue;
    if (c == QChar(0x00A0)) {
      out.append(QLatin1Char(' '));
      continue;
    }
    out.append(c);
  }
  return out;
}

struct Piece {
  QString text;
  bool is_image = false;
  bool is_break = false;  
  bool bold = false, italic = false, strike = false, code = false;
  QString href;
  QString alt, src;
};

std::vector<Piece> flatten_lines(std::vector<std::vector<Piece>>& lines) {
  std::vector<Piece> flat;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) {
      Piece br;
      br.is_break = true;
      flat.push_back(std::move(br));
    }
    for (auto& p : lines[i]) flat.push_back(std::move(p));
  }
  return flat;
}

void trim_trailing_space(std::vector<Piece>& line) {
  while (!line.empty() && !line.back().is_image) {
    const QString& t = line.back().text;
    qsizetype end = t.size();
    while (end > 0 && (t[end - 1] == QLatin1Char(' ') || t[end - 1] == QLatin1Char('\t'))) --end;
    if (end == t.size()) break;
    if (end == 0) {
      line.pop_back();
      continue;
    }
    line.back().text = t.left(end);
    break;
  }
}

void append_fragment(const QTextFragment& frag, bool in_heading, std::vector<std::vector<Piece>>& lines) {
  const QTextCharFormat cf = frag.charFormat();
  if (cf.isImageFormat()) {
    Piece p;
    p.is_image = true;
    const QTextImageFormat img = cf.toImageFormat();
    p.alt = img.property(QTextFormat::ImageAltText).toString();
    p.src = img.name();
    lines.back().push_back(std::move(p));
    return;
  }
  const bool bold = cf.fontWeight() >= QFont::DemiBold && !in_heading;
  const bool italic = cf.fontItalic();
  const bool strike = cf.fontStrikeOut();
  const bool code = cf.fontFamilies().toString().contains(QLatin1String("monospace"), Qt::CaseInsensitive);
  const QString href = cf.isAnchor() ? cf.anchorHref() : QString();
  const QString text = clean_text(frag.text());
  int start = 0;
  for (int i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == QChar::LineSeparator) {
      if (i > start) {
        Piece p;
        p.text = text.mid(start, i - start);
        p.bold = bold;
        p.italic = italic;
        p.strike = strike;
        p.code = code;
        p.href = href;
        lines.back().push_back(std::move(p));
      }
      if (i < text.size()) lines.push_back({});
      start = i + 1;
    }
  }
}

std::vector<std::vector<Piece>> block_lines(const QTextBlock& b, bool in_heading) {
  std::vector<std::vector<Piece>> lines;
  lines.push_back({});
  for (auto it = b.begin(); !it.atEnd(); ++it) {
    const QTextFragment frag = it.fragment();
    if (frag.isValid()) append_fragment(frag, in_heading, lines);
  }
  for (auto& line : lines) trim_trailing_space(line);
  return lines;
}

std::vector<Piece> cell_pieces(QTextDocument& qdoc, const QTextTableCell& cell) {
  std::vector<std::vector<Piece>> lines;
  lines.push_back({});
  const int last = cell.lastCursorPosition().position();
  for (QTextBlock b = qdoc.findBlock(cell.firstCursorPosition().position()); b.isValid() && b.position() < last;
       b = b.next()) {
    if (b.position() > cell.firstCursorPosition().position() && !lines.back().empty()) {
      Piece sp;
      sp.text = QStringLiteral(" ");
      lines.back().push_back(std::move(sp));
    }
    for (auto it = b.begin(); !it.atEnd(); ++it) {
      const QTextFragment frag = it.fragment();
      if (frag.isValid()) append_fragment(frag, false, lines);
    }
  }
  std::vector<Piece> flat;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i > 0 && !flat.empty()) {
      Piece sp;
      sp.text = QStringLiteral(" ");
      flat.push_back(std::move(sp));
    }
    for (auto& p : lines[i]) flat.push_back(std::move(p));
  }
  trim_trailing_space(flat);
  return flat;
}

struct Counts {
  std::size_t blocks = 0;
  std::size_t runs = 0;
  std::size_t dropped = 0;
};

struct Run {
  std::size_t start = 0, end = 0;
  bool is_image = false;
  bool bold = false, italic = false, strike = false, code = false;
  QString href;
  QString alt, src;
};

void apply_result(lumos::Document& scratch, lumos::Selection& cur, const lumos::CommandResult& r) {
  for (const auto& e : r.edits) scratch.apply(e);
  cur = r.selection;
}

void type_lines(lumos::Document& scratch, lumos::Selection& cur, std::string_view newline,
                 std::vector<Piece>& lines_flat, bool is_cell, Counts& counts) {
  std::vector<Run> runs;
  for (auto& piece : lines_flat) {
    if (piece.is_break) {
      auto r = lumos::insert_line_break(scratch, cur, newline);
      if (r) {
        apply_result(scratch, cur, *r);
      } else {
        counts.dropped++;
      }
      continue;
    }
    if (piece.is_image) {
      Run run;
      run.start = run.end = cur.cursor;
      run.is_image = true;
      run.alt = piece.alt;
      run.src = piece.src;
      runs.push_back(run);
      continue;
    }
    QString t = piece.text;
    if (is_cell) t.replace(QLatin1Char('|'), QLatin1String("\\|"));
    if (t.isEmpty()) continue;
    const std::string bytes = t.toStdString();
    scratch.apply(lumos::Edit{cur.cursor, 0, bytes});
    Run run;
    run.start = cur.cursor;
    run.end = cur.cursor + bytes.size();
    run.bold = piece.bold;
    run.italic = piece.italic;
    run.strike = piece.strike;
    run.code = piece.code;
    run.href = piece.href;
    cur = {run.end, run.end};
    if (run.bold || run.italic || run.strike || run.code || !run.href.isEmpty()) runs.push_back(run);
  }
  const std::size_t pre_marks_end = cur.cursor;
  std::ptrdiff_t growth = 0;
  const auto track = [&](const lumos::Edit& e) {
    scratch.apply(e);
    growth += static_cast<std::ptrdiff_t>(e.inserted.size()) - static_cast<std::ptrdiff_t>(e.removed);
  };
  for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
    if (it->is_image) {
      counts.runs++;
      bool placed = false;
      if (!it->src.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)) {
        auto r = lumos::insert_image(scratch, lumos::Selection{it->start, it->start}, it->alt.toStdString(),
                                      it->src.toStdString());
        if (r) {
          for (const auto& e : r->edits) track(e);
          placed = true;
        }
      }
      if (!placed) {
        counts.dropped++;
        if (!it->alt.isEmpty()) track(lumos::Edit{it->start, 0, it->alt.toStdString()});
      }
      continue;
    }
    lumos::Selection range{it->start, it->end};
    const auto wrap = [&](lumos::InlineKind kind) {
      counts.runs++;
      auto r = lumos::toggle_inline(scratch, range, kind);
      if (r) {
        for (const auto& e : r->edits) track(e);
        range = r->selection;
      } else {
        counts.dropped++;
      }
    };
    if (it->code) wrap(lumos::InlineKind::code);
    if (it->strike) wrap(lumos::InlineKind::strikethrough);
    if (it->italic) wrap(lumos::InlineKind::emphasis);
    if (it->bold) wrap(lumos::InlineKind::strong);
    if (!it->href.isEmpty()) {
      counts.runs++;
      const std::string url = it->href.toStdString();
      if (lumos::md::is_dangerous_url(url)) {
        counts.dropped++;
      } else {
        const std::size_t lo = std::min(range.anchor, range.cursor), hi = std::max(range.anchor, range.cursor);
        const std::string text = scratch.text().substr(lo, hi - lo);
        auto r = lumos::insert_link(scratch, range, text, url);
        if (r) {
          for (const auto& e : r->edits) track(e);
        } else {
          counts.dropped++;
        }
      }
    }
  }
  cur = {pre_marks_end + static_cast<std::size_t>(growth), pre_marks_end + static_cast<std::size_t>(growth)};
}

struct ListTracker {
  int depth = 0;
  bool ordered = false;
  std::vector<QTextList*> ptrs;
};

lumos::NodeId node_before(const lumos::SyntaxTree& tree, std::size_t offset) {
  return tree.node_at(offset > 0 ? offset - 1 : offset);
}

bool in_block_quote(const lumos::SyntaxTree& tree, std::size_t offset) {
  for (lumos::NodeId id = node_before(tree, offset); id != lumos::no_node; id = tree.node(id).parent)
    if (tree.node(id).kind == lumos::NodeKind::block_quote) return true;
  return false;
}

void list_depth_at(const lumos::SyntaxTree& tree, std::size_t offset, int& depth, bool& ordered) {
  depth = 0;
  ordered = false;
  for (lumos::NodeId id = node_before(tree, offset); id != lumos::no_node; id = tree.node(id).parent) {
    const lumos::Node& n = tree.node(id);
    if (n.kind == lumos::NodeKind::list) {
      if (depth == 0) ordered = n.ordered;
      ++depth;
    }
  }
}

void ensure_blank_line_plain(lumos::Document& scratch, lumos::Selection& cur, std::string_view newline) {
  const std::string& text = scratch.text();
  std::size_t p = cur.cursor;
  int found = 0;
  while (found < 2 && p >= newline.size() && text.compare(p - newline.size(), newline.size(), newline) == 0) {
    p -= newline.size();
    ++found;
  }
  const int need = p == 0 ? 0 : 2 - found;
  for (int i = 0; i < need; ++i) {
    scratch.apply(lumos::Edit{cur.cursor, 0, std::string(newline)});
    cur = {cur.cursor + newline.size(), cur.cursor + newline.size()};
  }
}

void trim_trailing_space(lumos::Document& scratch, lumos::Selection& cur) {
  const std::string& text = scratch.text();
  std::size_t p = cur.cursor;
  while (p > 0 && (text[p - 1] == ' ' || text[p - 1] == '\t')) --p;
  if (p == cur.cursor) return;
  scratch.apply(lumos::Edit{p, cur.cursor - p, {}});
  cur = {p, p};
}

void ensure_new_block(lumos::Document& scratch, lumos::Selection& cur, std::string_view newline, bool in_quote,
                       bool paragraph_follows) {
  trim_trailing_space(scratch, cur);
  if (!in_quote) {
    ensure_blank_line_plain(scratch, cur, newline);
    return;
  }
  auto r = lumos::insert_newline(scratch, cur, newline);
  if (r) apply_result(scratch, cur, *r);
  if (paragraph_follows) {
    auto r2 = lumos::insert_newline(scratch, cur, newline);
    if (r2) apply_result(scratch, cur, *r2);
  }
}

void exit_list(lumos::Document& scratch, lumos::Selection& cur, std::string_view newline, ListTracker& lt) {
  for (int i = 0; i < lt.depth + 1; ++i) {
    auto r = lumos::insert_newline(scratch, cur, newline);
    if (!r) break;
    apply_result(scratch, cur, *r);
  }
  lt = ListTracker{};
}

void ensure_plain(lumos::Document& scratch, lumos::Selection& cur, std::string_view newline, ListTracker& lt,
                   bool in_quote, bool paragraph_follows) {
  if (lt.depth > 0) exit_list(scratch, cur, newline, lt);
  ensure_new_block(scratch, cur, newline, in_quote, paragraph_follows);
}

void continue_list_item_paragraph(lumos::Document& scratch, lumos::Selection& cur, std::string_view newline) {
  auto r1 = lumos::insert_newline(scratch, cur, newline);
  if (!r1) return;
  apply_result(scratch, cur, *r1);
  auto r2 = lumos::delete_markup_backward(scratch, cur);
  if (!r2) return;
  apply_result(scratch, cur, *r2);
  const std::string& text = scratch.text();
  const std::size_t nl = text.rfind(newline, cur.cursor);
  if (nl == std::string::npos) return;
  const std::size_t insert_at = nl + newline.size();
  scratch.apply(lumos::Edit{insert_at, 0, std::string(newline)});
  cur = {cur.cursor + newline.size(), cur.cursor + newline.size()};
}

lumos::ByteRange ordered_marker_digits(const lumos::Document& scratch, std::size_t end) {
  const lumos::SyntaxTree& tree = scratch.tree();
  lumos::NodeId id = end >= 2 ? tree.node_at(end - 2) : lumos::no_node;
  while (id != lumos::no_node && tree.node(id).kind != lumos::NodeKind::list_marker) id = tree.node(id).parent;
  if (id == lumos::no_node) return {end, end};
  const lumos::Node& n = tree.node(id);
  return {n.start, n.end - 1};
}

void set_ordered_start(lumos::Document& scratch, lumos::Selection& cur, int start) {
  if (start == 1) return;
  const lumos::ByteRange digits = ordered_marker_digits(scratch, cur.cursor);
  if (digits.start == digits.end) return;
  const std::string text = std::to_string(start);
  const std::size_t old_len = digits.end - digits.start;
  scratch.apply(lumos::Edit{digits.start, old_len, text});
  cur = {cur.cursor + text.size() - old_len, cur.cursor + text.size() - old_len};
}

struct ListMove {
  bool toggle_before_type = false;
  bool toggle_after_type = false;
  int indent_after_type = 0;   
  int outdent_after_type = 0;  
  bool toggle_after_indent = false;
};

bool is_list_sibling(const QTextBlock& next, QTextList* qlist) {
  return next.isValid() && next.textList() == qlist;
}

ListMove move_to_list(lumos::Document& scratch, lumos::Selection& cur, std::string_view newline, ListTracker& lt,
                       int target_depth, bool target_ordered, QTextList* qlist_ptr, bool in_quote) {
  const bool same_list = lt.depth >= target_depth && target_depth >= 1 &&
                         (lt.ptrs.empty() ||
                          (static_cast<std::size_t>(target_depth) <= lt.ptrs.size() &&
                           lt.ptrs[static_cast<std::size_t>(target_depth - 1)] == qlist_ptr));
  if (lt.depth > 0 && !same_list) exit_list(scratch, cur, newline, lt);

  ListMove mv;
  if (lt.depth == 0) {
    if (in_quote) {
      trim_trailing_space(scratch, cur);
      auto r = lumos::insert_newline(scratch, cur, newline);
      if (r) apply_result(scratch, cur, *r);
      mv.toggle_after_type = true;
    } else {
      ensure_new_block(scratch, cur, newline,  false,  false);
      mv.toggle_before_type = true;
    }
    lt.depth = 1;
    lt.ordered = target_ordered;
    for (int d = 1; d < target_depth; ++d) ++mv.indent_after_type;  
    lt.depth = target_depth;
    lt.ptrs.assign(static_cast<std::size_t>(target_depth), qlist_ptr);
    return mv;
  }
  auto r = lumos::insert_newline(scratch, cur, newline);
  if (r) apply_result(scratch, cur, *r);
  if (target_depth == lt.depth) {
  } else if (target_depth > lt.depth) {
    mv.indent_after_type = target_depth - lt.depth;
    mv.toggle_after_indent = true;  
    lt.depth = target_depth;
  } else {
    mv.outdent_after_type = lt.depth - target_depth;
    mv.toggle_after_indent = true;
    lt.depth = target_depth;
  }
  lt.ordered = target_ordered;
  lt.ptrs.resize(static_cast<std::size_t>(target_depth));
  lt.ptrs[static_cast<std::size_t>(target_depth - 1)] = qlist_ptr;
  return mv;
}

}  

std::optional<lumos::CommandResult> paste_html(const lumos::Document& doc, lumos::Selection sel,
                                               const QString& html, std::string_view newline) {
  using lumos::BlockKind;

  const lumos::Format fmt_at_sel = lumos::format_at(doc, sel);
  if (fmt_at_sel.code_block || fmt_at_sel.table) return std::nullopt;

  QTextDocument qdoc;
  qdoc.setHtml(html);

  const std::size_t sel_start = std::min(sel.anchor, sel.cursor);
  const std::size_t sel_end = std::max(sel.anchor, sel.cursor);
  const bool in_quote = in_block_quote(doc.tree(), sel_start);
  int initial_list_depth = 0;
  bool initial_list_ordered = false;
  list_depth_at(doc.tree(), sel_start, initial_list_depth, initial_list_ordered);
  const bool has_trailing = sel_end < doc.size();

  lumos::Document scratch(doc.text(), doc.dialect());
  lumos::Selection cur{sel_start, sel_start};
  if (sel_start != sel_end) {
    scratch.apply(lumos::Edit{sel_start, sel_end - sel_start, {}});
    cur = {sel_start, sel_start};
  }

  ListTracker lt{initial_list_depth, initial_list_ordered, {}};
  bool list_untouched_by_paste = true;
  bool first_block = true;
  bool last_was_paragraph = false;

  Counts counts;
  bool skip_list_move = false;  
  QTextBlock b = qdoc.begin();
  while (b.isValid()) {
    if (QTextTable* table = qobject_cast<QTextTable*>(qdoc.frameAt(b.position()))) {
      ensure_plain(scratch, cur, newline, lt, in_quote,  false);
      first_block = false;
      last_was_paragraph = false;
      const int rows = table->rows(), cols = table->columns();
      auto r = lumos::insert_table(scratch, cur, rows, cols, newline);
      if (r) {
        apply_result(scratch, cur, *r);
        ++counts.blocks;
        for (int row = 0; row < rows; ++row) {
          for (int col = 0; col < cols; ++col) {
            if (row != 0 || col != 0) {
              auto nc = lumos::next_cell(scratch, cur, newline);
              if (nc) apply_result(scratch, cur, *nc);
            }
            std::vector<Piece> pieces = cell_pieces(qdoc, table->cellAt(row, col));
            type_lines(scratch, cur, newline, pieces,  true, counts);
          }
        }
        auto ft = lumos::format_table(scratch, cur);
        if (ft) apply_result(scratch, cur, *ft);
      }
      b = qdoc.findBlock(table->lastPosition());
      b = b.next();
      continue;
    }

    if (b.blockFormat().nonBreakableLines()) {
      std::vector<QString> code_lines;
      QTextBlock cb = b;
      while (cb.isValid() && cb.blockFormat().nonBreakableLines()) {
        code_lines.push_back(clean_text(cb.text()));
        cb = cb.next();
      }
      ensure_plain(scratch, cur, newline, lt, in_quote,  false);
      first_block = false;
      last_was_paragraph = false;
      std::string joined;
      for (std::size_t i = 0; i < code_lines.size(); ++i) {
        if (i > 0) joined += newline;
        joined += code_lines[i].toStdString();
      }
      const std::size_t start = cur.cursor;
      scratch.apply(lumos::Edit{start, 0, joined});
      cur = {start + joined.size(), start + joined.size()};
      auto r = lumos::toggle_block(scratch, lumos::Selection{start, cur.cursor}, BlockKind::code_block);
      if (r) apply_result(scratch, cur, *r);
      ++counts.blocks;
      b = cb;
      continue;
    }

    if (b.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth)) {
      ensure_plain(scratch, cur, newline, lt, in_quote,  false);
      first_block = false;
      last_was_paragraph = false;
      auto r = lumos::insert_thematic_break(scratch, cur);
      if (r) {
        apply_result(scratch, cur, *r);
        ++counts.blocks;
      }
      b = b.next();
      continue;
    }

    const int heading_level = b.blockFormat().headingLevel();
    if (heading_level > 0) {
      ensure_plain(scratch, cur, newline, lt, in_quote,  false);
      first_block = false;
      last_was_paragraph = false;
      auto r = lumos::set_heading(scratch, cur, heading_level);
      if (r) {
        apply_result(scratch, cur, *r);
        std::vector<std::vector<Piece>> lines = block_lines(b,  true);
        std::vector<Piece> flat = flatten_lines(lines);
        type_lines(scratch, cur, newline, flat,  false, counts);
        ++counts.blocks;
      }
      b = b.next();
      continue;
    }

    if (QTextList* qlist = b.textList()) {
      first_block = false;
      last_was_paragraph = false;
      list_untouched_by_paste = false;
      const QTextListFormat lf = qlist->format();
      const int target_depth = lf.indent();
      const bool target_ordered = lf.style() <= QTextListFormat::ListDecimal;
      const int start_number = lf.start();
      ListMove mv;
      if (skip_list_move) {
        skip_list_move = false;  
      } else {
        mv = move_to_list(scratch, cur, newline, lt, target_depth, target_ordered, qlist, in_quote);
        if (mv.toggle_before_type) {
          auto r = lumos::toggle_block(scratch, cur, target_ordered ? BlockKind::ordered_list : BlockKind::bullet_list);
          if (r) apply_result(scratch, cur, *r);
          if (target_ordered) set_ordered_start(scratch, cur, start_number);
        }
      }
      std::vector<std::vector<Piece>> lines = block_lines(b,  false);
      const bool open_next_shell = lines.size() > 1 && mv.indent_after_type == 0 && mv.outdent_after_type == 0 &&
                                   is_list_sibling(b.next(), qlist);
      if (open_next_shell) {
        std::vector<Piece> line0 = lines.front();
        type_lines(scratch, cur, newline, line0,  false, counts);
        const std::size_t p = cur.cursor;
        auto r = lumos::insert_newline(scratch, cur, newline);
        std::size_t shell_end = p;
        if (r) {
          apply_result(scratch, cur, *r);
          shell_end = cur.cursor;
          skip_list_move = true;
        }
        cur = {p, p};
        std::vector<std::vector<Piece>> rest(lines.begin() + 1, lines.end());
        std::vector<Piece> flat_rest = flatten_lines(rest);
        Piece lead_break;
        lead_break.is_break = true;
        flat_rest.insert(flat_rest.begin(), lead_break);
        type_lines(scratch, cur, newline, flat_rest,  false, counts);
        const std::size_t grew = cur.cursor - p;
        cur = {shell_end + grew, shell_end + grew};
      } else {
        std::vector<Piece> flat = flatten_lines(lines);
        type_lines(scratch, cur, newline, flat,  false, counts);
      }
      if (mv.toggle_after_type) {
        auto r = lumos::toggle_block(scratch, cur, target_ordered ? BlockKind::ordered_list : BlockKind::bullet_list);
        if (r) apply_result(scratch, cur, *r);
      }
      for (int i = 0; i < mv.indent_after_type; ++i) {
        auto r = lumos::indent_list(scratch, cur);
        if (r) apply_result(scratch, cur, *r);
      }
      for (int i = 0; i < mv.outdent_after_type; ++i) {
        auto r = lumos::outdent_list(scratch, cur);
        if (r) apply_result(scratch, cur, *r);
      }
      if (mv.toggle_after_indent) {
        lumos::Format now = lumos::format_at(scratch, cur);
        const bool now_ordered = now.list == lumos::ListKind::ordered;
        if (now_ordered != target_ordered) {
          auto r = lumos::toggle_block(scratch, cur, target_ordered ? BlockKind::ordered_list : BlockKind::bullet_list);
          if (r) apply_result(scratch, cur, *r);
        }
      }
      ++counts.blocks;
      b = b.next();
      continue;
    }

    if (b.blockFormat().intProperty(QTextFormat::BlockQuoteLevel) > 0) {
      ensure_plain(scratch, cur, newline, lt, in_quote,  false);
      first_block = false;
      last_was_paragraph = false;
      auto r = lumos::toggle_block(scratch, cur, BlockKind::block_quote);
      if (r) {
        apply_result(scratch, cur, *r);
        std::vector<std::vector<Piece>> lines = block_lines(b,  false);
        std::vector<Piece> flat = flatten_lines(lines);
        type_lines(scratch, cur, newline, flat,  false, counts);
        ++counts.blocks;
      }
      b = b.next();
      continue;
    }

    std::vector<std::vector<Piece>> lines = block_lines(b,  false);
    bool any_text = false;
    for (auto& line : lines)
      for (auto& p : line)
        if (p.is_image || !p.text.isEmpty()) any_text = true;
    if (!any_text) {
      b = b.next();
      continue;
    }
    if (first_block) {
    } else if (list_untouched_by_paste && lt.depth > 0) {
      continue_list_item_paragraph(scratch, cur, newline);
    } else {
      ensure_plain(scratch, cur, newline, lt, in_quote,  true);
    }
    first_block = false;
    last_was_paragraph = true;
    std::vector<Piece> flat = flatten_lines(lines);
    type_lines(scratch, cur, newline, flat,  false, counts);
    ++counts.blocks;
    b = b.next();
  }

  if (counts.blocks == 0) return std::nullopt;

  const std::size_t paste_end = cur.cursor;
  if (has_trailing && !last_was_paragraph) ensure_plain(scratch, cur, newline, lt, in_quote, false);

  const std::string& old_text = doc.text();
  const std::string& new_text = scratch.text();
  std::size_t prefix = 0;
  const std::size_t max_prefix = std::min(old_text.size(), new_text.size());
  while (prefix < max_prefix && old_text[prefix] == new_text[prefix]) ++prefix;
  std::size_t suffix = 0;
  const std::size_t max_suffix = max_prefix - prefix;
  while (suffix < max_suffix && old_text[old_text.size() - 1 - suffix] == new_text[new_text.size() - 1 - suffix])
    ++suffix;

  lumos::CommandResult result;
  result.edits.push_back(lumos::Edit{prefix, old_text.size() - prefix - suffix,
                                     new_text.substr(prefix, new_text.size() - prefix - suffix)});
  const std::size_t final_cursor = std::min(paste_end, new_text.size() - suffix);
  result.selection = {final_cursor, final_cursor};

  lumos::log(lumos::LogLevel::debug, kComp, "paste_html",
             {{"blocks", std::to_string(counts.blocks)},
              {"runs", std::to_string(counts.runs)},
              {"dropped", std::to_string(counts.dropped)},
              {"bytes", std::to_string(html.toUtf8().size())}});
  return result;
}
