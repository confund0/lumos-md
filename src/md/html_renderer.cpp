#include "lumos/md/html_renderer.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "md/entities.hpp"
#include "md/link_resolve.hpp"
#include "md/scanners.hpp"
#include "md/unicode.hpp"

namespace lumos::md {

namespace {

void append_escaped_byte(char c, std::string& out) {
  switch (c) {
    case '&': out += "&amp;"; break;
    case '<': out += "&lt;"; break;
    case '>': out += "&gt;"; break;
    case '"': out += "&quot;"; break;
    case '\0': out += "\xEF\xBF\xBD"; break;
    default: out += c;
  }
}

void append_escaped_run(std::string_view s, std::string& out) {
  static constexpr std::string_view special("&<>\"\0", 5);
  std::size_t from = 0;
  for (std::size_t at = s.find_first_of(special); at != std::string_view::npos; at = s.find_first_of(special, from)) {
    out.append(s.substr(from, at - from));
    append_escaped_byte(s[at], out);
    from = at + 1;
  }
  out.append(s.substr(from));
}

void cr(std::string& out) {
  if (!out.empty() && out.back() != '\n') out += '\n';
}

bool is_tag(std::string_view tag, std::string_view name) noexcept {
  if (tag.size() < 3 || tag[0] != '<') return false;
  std::size_t i = 1;
  if (tag[i] == '/') ++i;
  std::size_t k = 0;
  for (; i < tag.size(); ++i, ++k) {
    if (k == name.size()) break;
    if (std::tolower(static_cast<unsigned char>(tag[i])) != static_cast<unsigned char>(name[k])) return false;
  }
  if (i == tag.size()) return false;
  if (is_cmark_space(tag[i]) || tag[i] == '>') return true;
  if (tag[i] == '/' && i + 1 < tag.size() && tag[i + 1] == '>') return true;
  return false;
}

bool is_blacklisted_tag(std::string_view tag) noexcept {
  static constexpr std::string_view names[] = {"title", "textarea", "style",    "xmp",   "iframe",
                                               "noembed", "noframes", "script", "plaintext"};
  for (std::string_view name : names)
    if (is_tag(tag, name)) return true;
  return false;
}

void apply_tag_filter(std::string_view text, std::string& out) {
  for (std::size_t i = 0; i < text.size();) {
    if (text[i] != '<') {
      out += text[i];
      ++i;
      continue;
    }
    out += is_blacklisted_tag(text.substr(i)) ? "&lt;" : "<";
    ++i;
  }
}

std::string normalize_code_span(const SyntaxTree& tree, const TextSource& text, NodeId id, std::size_t from,
                                std::size_t to) {
  std::string s;
  bool pending_cr = false;
  walk_inline_bytes(tree, text, id, from, to, [&](char ch) {
    if (ch == '\n') {
      s += ' ';
      pending_cr = false;
      return;
    }
    if (ch == '\r') {
      if (pending_cr) s += ' ';
      pending_cr = true;
      return;
    }
    if (pending_cr) {
      s += ' ';
      pending_cr = false;
    }
    if (ch == '\0') s += "\xEF\xBF\xBD";
    else s += ch;
  });
  if (pending_cr) s += ' ';
  const bool contains_nonspace = s.find_first_not_of(' ') != std::string::npos;
  if (contains_nonspace && s.size() >= 2 && s.front() == ' ' && s.back() == ' ') s = s.substr(1, s.size() - 2);
  return s;
}

void append_literal(const SyntaxTree& tree, const TextSource& text, NodeId id, bool escape, std::string& out) {
  const Node& n = tree.node(id);
  bool prev_cr = false;
  auto put = [&](std::string_view c) {
    for (char ch : c) {
      if (prev_cr && ch == '\n') {
        prev_cr = false;
        continue;
      }
      prev_cr = ch == '\r';
      if (ch == '\r') {
        out += '\n';
      } else if (escape) {
        append_escaped_byte(ch, out);
      } else if (ch == '\0') {
        out += "\xEF\xBF\xBD";
      } else {
        out += ch;
      }
    }
  };
  out.append(n.virtual_spaces, ' ');
  std::size_t pos = n.start;
  bool ends_with_prefix = false;
  for (NodeId c : tree.children(id)) {
    const Node& k = tree.node(c);
    if (k.start > pos) {
      for_chunks(text, pos, k.start, put);
      ends_with_prefix = false;
    }
    prev_cr = false;
    out.append(k.virtual_spaces, ' ');
    pos = k.end;
    ends_with_prefix = true;
  }
  if (n.end > pos) {
    for_chunks(text, pos, n.end, put);
    ends_with_prefix = false;
  }
  if (n.end > n.start) {
    if (ends_with_prefix) {
      out += '\n';
    } else {
      const std::string last = slice(text, n.end - 1, n.end);
      if (last != "\n" && last != "\r") out += '\n';
    }
  }
}

std::string info_word(const std::string& raw) {
  std::string decoded;
  for (std::size_t i = 0; i < raw.size();) {
    if (raw[i] == '&') {
      const std::size_t n = scan_entity(raw, i, &decoded);
      if (n > 0) {
        i += n;
        continue;
      }
    }
    decoded += raw[i++];
  }
  std::size_t a = 0, b = decoded.size();
  while (a < b && is_cmark_space(decoded[a])) ++a;
  while (b > a && is_cmark_space(decoded[b - 1])) --b;
  std::string info;
  for (std::size_t i = a; i < b; ++i) {
    if (decoded[i] == '\\' && i + 1 < b && is_ascii_punctuation(decoded[i + 1])) ++i;
    info += decoded[i];
  }
  std::size_t w = 0;
  while (w < info.size() && !is_cmark_space(info[w])) ++w;
  return info.substr(0, w);
}

long list_start(const SyntaxTree& tree, const TextSource& text, NodeId list) {
  for (NodeId item : tree.children(list)) {
    for (NodeId m : tree.children(item)) {
      if (tree.node(m).kind != NodeKind::list_marker) continue;
      const std::string s = slice(text, tree.node(m).start, tree.node(m).end);
      long v = 0;
      for (char c : s) {
        if (c < '0' || c > '9') break;
        v = v * 10 + (c - '0');
      }
      return v;
    }
    break;
  }
  return 1;
}

bool in_tight_list(const SyntaxTree& tree, NodeId paragraph) {
  const NodeId item = tree.node(paragraph).parent;
  if (item == no_node || tree.node(item).kind != NodeKind::list_item) return false;
  const NodeId list = tree.node(item).parent;
  return list != no_node && tree.node(list).kind == NodeKind::list && tree.node(list).tight;
}

}  

bool is_dangerous_url(std::string_view url) noexcept {
  const auto starts_with = [](std::string_view s, std::string_view lower_prefix) {
    if (s.size() < lower_prefix.size()) return false;
    for (std::size_t i = 0; i < lower_prefix.size(); ++i) {
      char c = s[i];
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
      if (c != lower_prefix[i]) return false;
    }
    return true;
  };
  static constexpr std::string_view safe_data = "data:image/";
  if (starts_with(url, safe_data)) {
    const std::string_view rest = url.substr(safe_data.size());
    for (std::string_view ext : {"png", "gif", "jpeg", "webp"})
      if (starts_with(rest, ext)) return false;
  }
  for (std::string_view scheme : {"javascript:", "vbscript:", "file:", "data:"})
    if (starts_with(url, scheme)) return true;
  return false;
}

namespace {

void append_plain_text(const SyntaxTree& tree, const TextSource& text, NodeId root, std::string& out) {
  const NodeId end = tree.node(root).subtree_end;
  NodeId i = root;
  while (i < end) {
    const Node& k = tree.node(i);
    switch (k.kind) {
      case NodeKind::text:
        append_escaped_run(raw_inline_text(tree, text, i, k.start, k.end), out);
        i = k.subtree_end;
        break;
      case NodeKind::code_span: {
        const NodeId t = first_text_child(tree, i);
        const Node& tk = tree.node(t);
        append_escaped_run(normalize_code_span(tree, text, t, tk.start, tk.end), out);
        i = k.subtree_end;
        break;
      }
      case NodeKind::html_inline:
        append_escaped_run(raw_inline_text(tree, text, i, k.start, k.end), out);
        i = k.subtree_end;
        break;
      case NodeKind::entity: {
        std::string decoded;
        scan_entity(slice(text, k.start, k.end), 0, &decoded);
        append_escaped_run(decoded, out);
        i = k.subtree_end;
        break;
      }
      case NodeKind::escape: {
        const std::string raw = raw_inline_text(tree, text, i, k.start, k.end);
        if (raw.size() == 2) append_escaped_byte(raw[1], out);
        i = k.subtree_end;
        break;
      }
      case NodeKind::soft_break:
      case NodeKind::hard_break:
        out += ' ';
        i = k.subtree_end;
        break;
      case NodeKind::link_delimiter:
      case NodeKind::link_destination:
      case NodeKind::link_title:
      case NodeKind::link_label:
        i = k.subtree_end;  
        break;
      default:
        ++i;  
        break;
    }
  }
}

class Renderer {
 public:
  Renderer(const SyntaxTree& tree, const TextSource& text, const HtmlOptions& options, std::string& out)
      : tree_(tree), text_(text), options_(options), out_(out) {}

  void run() { run_range(0, static_cast<NodeId>(tree_.size())); }

 private:
  struct Open {
    NodeId id;
    bool by_hook;  
  };
  void run_range(NodeId lo, NodeId hi) {
    std::vector<Open> open;
    NodeId i = lo;
    while (i < hi) {
      while (!open.empty() && tree_.node(open.back().id).subtree_end <= i) {
        close(open.back());
        open.pop_back();
      }
      const NodeOutput hooked =
          options_.node_hook ? options_.node_hook(tree_, text_, i, true, out_) : NodeOutput::keep;
      if (hooked == NodeOutput::replace_tags) {
        open.push_back({i, true});
        ++i;
      } else if (hooked == NodeOutput::keep && enter(i)) {
        open.push_back({i, false});
        ++i;
      } else {
        i = tree_.node(i).subtree_end;
      }
    }
    while (!open.empty()) {
      close(open.back());
      open.pop_back();
    }
  }

  void close(const Open& o) {
    if (o.by_hook) {
      options_.node_hook(tree_, text_, o.id, false, out_);
    } else {
      leave(o.id);
    }
  }

  std::optional<std::string> final_url(UrlKind kind, std::string url) const {
    if (options_.url_hook) return options_.url_hook(kind, url);
    if (!options_.unsafe && is_dangerous_url(url)) return std::string();
    return url;
  }

  bool enter(NodeId id) {
    const Node& n = tree_.node(id);
    switch (n.kind) {
      case NodeKind::document: return true;
      case NodeKind::block_quote:
        cr(out_);
        out_ += "<blockquote>\n";
        return true;
      case NodeKind::list:
        cr(out_);
        if (!n.ordered) {
          out_ += "<ul>\n";
        } else {
          const long start = list_start(tree_, text_, id);
          if (start == 1) {
            out_ += "<ol>\n";
          } else {
            out_ += "<ol start=\"" + std::to_string(start) + "\">\n";
          }
        }
        return true;
      case NodeKind::list_item:
        cr(out_);
        out_ += "<li>";
        write_task_checkbox(id);
        return true;
      case NodeKind::heading:
        cr(out_);
        out_ += "<h";
        out_ += static_cast<char>('0' + n.level);
        out_ += '>';
        return true;
      case NodeKind::paragraph:
        if (!in_tight_list(tree_, id)) {
          cr(out_);
          out_ += "<p>";
        }
        return true;
      case NodeKind::thematic_break:
        cr(out_);
        out_ += "<hr />\n";
        return false;
      case NodeKind::code_block: code_block(id); return false;
      case NodeKind::table: table_enter(id); return true;
      case NodeKind::table_header_row: table_row(id,  true); return false;
      case NodeKind::table_row: table_row(id,  false); return false;
      case NodeKind::table_delimiter_row: return false;  
      case NodeKind::html_block:
        cr(out_);
        if (options_.unsafe) {
          if (options_.tagfilter) {
            std::string buf;
            append_literal(tree_, text_, id, false, buf);
            apply_tag_filter(buf, out_);
          } else {
            append_literal(tree_, text_, id, false, out_);
          }
        } else {
          out_ += "<!-- raw HTML omitted -->";
        }
        cr(out_);
        return false;
      case NodeKind::text:
        append_escaped_run(raw_inline_text(tree_, text_, id, n.start, n.end), out_);
        return false;
      case NodeKind::soft_break:
        out_ += options_.hard_breaks ? "<br />\n" : "\n";
        return false;
      case NodeKind::hard_break: out_ += "<br />\n"; return false;
      case NodeKind::entity: out_entity(n); return false;
      case NodeKind::escape: {
        const std::string raw = raw_inline_text(tree_, text_, id, n.start, n.end);
        if (raw.size() == 2) append_escaped_byte(raw[1], out_);
        return false;
      }
      case NodeKind::code_span: code_span(id); return false;
      case NodeKind::autolink: return autolink(id);
      case NodeKind::html_inline: {
        if (!options_.unsafe) {
          out_ += "<!-- raw HTML omitted -->";
          return false;
        }
        const std::string raw = raw_inline_text(tree_, text_, id, n.start, n.end);
        if (options_.tagfilter && is_blacklisted_tag(raw)) {
          out_ += "&lt;";
          out_.append(raw, 1, raw.size() - 1);
        } else {
          out_ += raw;
        }
        return false;
      }
      case NodeKind::emphasis:
        out_ += "<em>";
        return true;
      case NodeKind::strong:
        out_ += "<strong>";
        return true;
      case NodeKind::strikethrough:
        out_ += "<del>";
        return true;
      case NodeKind::link: return link_enter(id);
      case NodeKind::image: return !image_enter(id);
      default:
        return node_class(n.kind) == NodeClass::structure;
    }
  }

  void leave(NodeId id) {
    const Node& n = tree_.node(id);
    switch (n.kind) {
      case NodeKind::block_quote:
        cr(out_);
        out_ += "</blockquote>\n";
        break;
      case NodeKind::list: out_ += n.ordered ? "</ol>\n" : "</ul>\n"; break;
      case NodeKind::list_item: out_ += "</li>\n"; break;
      case NodeKind::heading:
        out_ += "</h";
        out_ += static_cast<char>('0' + n.level);
        out_ += ">\n";
        break;
      case NodeKind::paragraph:
        if (!in_tight_list(tree_, id)) out_ += "</p>\n";
        break;
      case NodeKind::emphasis: out_ += "</em>"; break;
      case NodeKind::strong: out_ += "</strong>"; break;
      case NodeKind::strikethrough: out_ += "</del>"; break;
      case NodeKind::autolink:
      case NodeKind::link:
        if (!link_wrapped_.empty()) {
          if (link_wrapped_.back()) out_ += "</a>";
          link_wrapped_.pop_back();
        }
        break;
      case NodeKind::table:
        if (table_need_closing_tbody_) {
          cr(out_);
          out_ += "</tbody>\n";
          table_need_closing_tbody_ = false;
        }
        cr(out_);
        out_ += "</table>\n";
        break;
      default: break;
    }
  }

  void out_entity(const Node& n) {
    const std::string raw = slice(text_, n.start, n.end);
    std::string decoded;
    scan_entity(raw, 0, &decoded);
    append_escaped_run(decoded, out_);
  }

  bool autolink(NodeId id) {
    const Node& n = tree_.node(id);
    if (n.variant == autolink_variant_extended_email || n.variant == autolink_variant_extended_email_plain) {
      const std::optional<std::string> url = final_url(UrlKind::link, autolink_url(tree_, text_, id));
      if (url) {
        out_ += "<a href=\"";
        append_escaped_href(*url, out_);
        out_ += "\">";
      }
      link_wrapped_.push_back(url.has_value());
      return true;
    }
    const NodeId text_id = inner_text(id);
    const Node& t = tree_.node(text_id);
    const std::string inner = n.variant == autolink_variant_www || n.variant == autolink_variant_url
                                   ? raw_inline_text(tree_, text_, text_id, t.start, t.end)
                                   : slice(text_, t.start, t.end);
    const std::optional<std::string> url = final_url(UrlKind::link, autolink_url(tree_, text_, id));
    if (url) {
      out_ += "<a href=\"";
      append_escaped_href(*url, out_);
      out_ += "\">";
    }
    append_escaped_run(inner, out_);
    if (url) out_ += "</a>";
    return false;
  }

  NodeId inner_text(NodeId id) const { return first_text_child(tree_, id); }

  void code_span(NodeId id) {
    const NodeId t = inner_text(id);
    const Node& k = tree_.node(t);
    out_ += "<code>";
    append_escaped_run(normalize_code_span(tree_, text_, t, k.start, k.end), out_);
    out_ += "</code>";
  }

  Resolved resolve_link(NodeId id) { return resolve_reference(tree_, text_, id, ref_defs()); }

  const std::unordered_map<std::string, RefDef>& ref_defs() {
    if (!ref_defs_built_) {
      ref_defs_ = build_ref_defs(tree_, text_);
      ref_defs_built_ = true;
    }
    return ref_defs_;
  }

  bool link_enter(NodeId id) {
    Resolved r = resolve_link(id);
    std::optional<std::string> url;
    if (r.wrap) url = final_url(UrlKind::link, std::move(r.href));
    r.wrap = url.has_value();
    if (r.wrap) {
      out_ += "<a href=\"";
      append_escaped_href(*url, out_);
      if (r.has_title) {
        out_ += "\" title=\"";
        append_escaped_run(r.title, out_);
      }
      out_ += "\">";
    }
    link_wrapped_.push_back(r.wrap);
    return true;  
  }

  bool image_enter(NodeId id) {
    Resolved r = resolve_link(id);
    if (!r.wrap) return false;
    const std::optional<std::string> url = final_url(UrlKind::image, std::move(r.href));
    if (!url) return false;
    std::string alt;
    append_plain_text(tree_, text_, id, alt);
    out_ += "<img src=\"";
    append_escaped_href(*url, out_);
    out_ += "\" alt=\"";
    out_ += alt;
    if (r.has_title) {
      out_ += "\" title=\"";
      append_escaped_run(r.title, out_);
    }
    out_ += "\" />";
    return true;
  }

  void write_task_checkbox(NodeId item) {
    for (NodeId c : tree_.children(item)) {
      if (tree_.node(c).kind != NodeKind::task_marker) continue;
      out_ += tree_.node(c).checked ? "<input type=\"checkbox\" checked=\"\" disabled=\"\" /> "
                                    : "<input type=\"checkbox\" disabled=\"\" /> ";
      return;
    }
  }

  void code_block(NodeId id) {
    cr(out_);
    NodeId content = no_node;
    std::string word;
    for (NodeId c : tree_.children(id)) {
      const Node& k = tree_.node(c);
      if (k.kind == NodeKind::info_string) word = info_word(slice(text_, k.start, k.end));
      if (k.kind == NodeKind::code_content) content = c;
    }
    if (word.empty()) {
      out_ += "<pre><code>";
    } else {
      out_ += "<pre><code class=\"";
      if (word.compare(0, 9, "language-") != 0) out_ += "language-";
      for (char ch : word) append_escaped_byte(ch, out_);
      out_ += "\">";
    }
    if (content != no_node) append_literal(tree_, text_, content, true, out_);
    out_ += "</code></pre>\n";
  }

  std::vector<NodeId> row_slots(NodeId row) {
    std::vector<NodeId> slots;
    bool open_slot = false;
    bool first = true;
    NodeId current = no_node;
    for (NodeId c : tree_.children(row)) {
      const NodeKind k = tree_.node(c).kind;
      if (k == NodeKind::table_pipe) {
        if (!first || tree_.node(c).start != tree_.node(row).start) slots.push_back(current);
        open_slot = true;
        current = no_node;
      } else if (k == NodeKind::table_cell || k == NodeKind::table_delimiter_cell) {
        current = c;
        open_slot = true;
      }
      first = false;
    }
    if (open_slot && current != no_node) slots.push_back(current);
    return slots;
  }

  void table_enter(NodeId id) {
    cr(out_);
    out_ += "<table>";
    table_need_closing_tbody_ = false;
    table_align_.clear();
    for (NodeId c : tree_.children(id)) {
      if (tree_.node(c).kind != NodeKind::table_delimiter_row) continue;
      for (NodeId cell : row_slots(c)) table_align_.push_back(tree_.node(cell).align);
      break;
    }
  }

  void table_row(NodeId row, bool is_header) {
    cr(out_);
    if (is_header) {
      out_ += "<thead>\n";
    } else if (!table_need_closing_tbody_) {
      out_ += "<tbody>\n";
      table_need_closing_tbody_ = true;
    }
    out_ += "<tr>";
    const std::vector<NodeId> slots = row_slots(row);
    for (std::size_t col = 0; col < table_align_.size(); ++col) {
      const NodeId cell = col < slots.size() ? slots[col] : no_node;
      cr(out_);
      out_ += is_header ? "<th" : "<td";
      switch (table_align_[col]) {
        case Alignment::left: out_ += " align=\"left\""; break;
        case Alignment::center: out_ += " align=\"center\""; break;
        case Alignment::right: out_ += " align=\"right\""; break;
        case Alignment::none: break;
      }
      out_ += ">";
      if (cell != no_node) run_range(cell + 1, tree_.node(cell).subtree_end);
      out_ += is_header ? "</th>" : "</td>";
    }
    cr(out_);
    out_ += "</tr>";
    if (is_header) {
      cr(out_);
      out_ += "</thead>";
    }
  }

  const SyntaxTree& tree_;
  const TextSource& text_;
  const HtmlOptions& options_;
  std::string& out_;
  bool ref_defs_built_ = false;
  std::unordered_map<std::string, RefDef> ref_defs_;
  std::vector<bool> link_wrapped_;
  std::vector<Alignment> table_align_;       
  bool table_need_closing_tbody_ = false;
};

}  

void append_escaped_html(std::string_view text, std::string& out) { append_escaped_run(text, out); }

void append_escaped_href(std::string_view url, std::string& out) {
  static constexpr char hex[] = "0123456789ABCDEF";
  for (char ch : url) {
    const auto c = static_cast<unsigned char>(ch);
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      std::string_view("-_.!~*()#$%+,/:;=?@").find(ch) != std::string_view::npos;
    if (c == '&') {
      out += "&amp;";
    } else if (c == '\'') {
      out += "&#x27;";
    } else if (safe) {
      out += ch;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
}

void render_html(const SyntaxTree& tree, const TextSource& text, const HtmlOptions& options, std::string& out) {
  Renderer(tree, text, options, out).run();
}

std::string render_html(const SyntaxTree& tree, const TextSource& text, const HtmlOptions& options) {
  std::string out;
  render_html(tree, text, options, out);
  return out;
}

}  
