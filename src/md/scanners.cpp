#include "md/scanners.hpp"

#include "md/unicode.hpp"

namespace lumos::md {

namespace {

constexpr bool is_alpha(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
constexpr bool is_alnum(char c) noexcept { return is_alpha(c) || is_digit(c); }
constexpr char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }

bool iequals(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (lower(a[i]) != lower(b[i])) return false;
  return true;
}

bool icontains(std::string_view s, std::string_view needle) noexcept {
  if (needle.size() > s.size()) return false;
  for (std::size_t i = 0; i + needle.size() <= s.size(); ++i)
    if (iequals(s.substr(i, needle.size()), needle)) return true;
  return false;
}

constexpr std::string_view block_tag_names[] = {
    "address", "article",  "aside",    "base",       "basefont", "blockquote", "body",     "caption",
    "center",  "col",      "colgroup", "dd",         "details",  "dialog",     "dir",      "div",
    "dl",      "dt",       "fieldset", "figcaption", "figure",   "footer",     "form",     "frame",
    "frameset", "h1",      "h2",       "h3",         "h4",       "h5",         "h6",       "head",
    "header",  "hr",       "html",     "iframe",     "legend",   "li",         "link",     "main",
    "menu",    "menuitem", "nav",      "noframes",   "ol",       "optgroup",   "option",   "p",
    "param",   "search",   "section",  "summary",    "table",    "tbody",      "td",       "tfoot",
    "th",      "thead",    "title",    "tr",         "track",    "ul"};

bool is_block_tag_name(std::string_view name) noexcept {
  for (std::string_view t : block_tag_names)
    if (iequals(name, t)) return true;
  return false;
}

constexpr bool is_html_space(char c) noexcept { return is_cmark_space(c); }

std::size_t skip_html_spaces(std::string_view s, std::size_t i) noexcept {
  while (i < s.size() && is_html_space(s[i])) ++i;
  return i;
}

std::size_t scan_tag_name(std::string_view s, std::size_t i) noexcept {
  if (i >= s.size() || !is_alpha(s[i])) return 0;
  std::size_t p = i + 1;
  while (p < s.size() && (is_alnum(s[p]) || s[p] == '-')) ++p;
  return p - i;
}

std::size_t scan_attribute_name(std::string_view s, std::size_t i) noexcept {
  if (i >= s.size() || !(is_alpha(s[i]) || s[i] == '_' || s[i] == ':')) return 0;
  std::size_t p = i + 1;
  while (p < s.size() && (is_alnum(s[p]) || s[p] == ':' || s[p] == '.' || s[p] == '_' || s[p] == '-')) ++p;
  return p - i;
}

std::size_t scan_attribute_value(std::string_view s, std::size_t i) noexcept {
  if (i >= s.size()) return i;
  const char q = s[i];
  if (q == '\'' || q == '"') {
    const std::size_t close = s.find(q, i + 1);
    return close == std::string_view::npos ? i : close + 1;
  }
  std::size_t p = i;
  while (p < s.size()) {
    const char c = s[p];
    if (is_html_space(c) || c == '"' || c == '\'' || c == '=' || c == '<' || c == '>' || c == '`') break;
    ++p;
  }
  return p;
}

}  

bool scan_thematic_break(std::string_view s, std::size_t* fail_at) noexcept {
  const char c = s.empty() ? '\0' : s[0];
  if (c != '*' && c != '_' && c != '-') {
    *fail_at = 0;
    return false;
  }
  std::size_t count = 1;
  std::size_t i = 1;
  for (; i < s.size(); ++i) {
    if (s[i] == c) {
      ++count;
    } else if (s[i] != ' ' && s[i] != '\t') {
      break;
    }
  }
  if (count >= 3 && i == s.size()) return true;
  *fail_at = i;
  return false;
}

std::uint8_t scan_atx_heading_start(std::string_view s, std::size_t* content_start) noexcept {
  std::size_t n = 0;
  while (n < s.size() && s[n] == '#') ++n;
  if (n == 0 || n > 6) return 0;
  if (n < s.size() && s[n] != ' ' && s[n] != '\t') return 0;
  std::size_t p = n;
  while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
  *content_start = p;
  return static_cast<std::uint8_t>(n);
}

AtxContent scan_atx_content(std::string_view s, std::size_t from) noexcept {
  AtxContent r;
  std::size_t end = s.size();
  while (end > from && is_cmark_space(s[end - 1])) --end;
  r.content_end = end;
  r.closing_start = r.closing_end = end;
  std::size_t k = end;
  while (k > from && s[k - 1] == '#') --k;
  if (k == end) return r;
  if (k == from) {
    r.content_end = from;
    r.closing_start = k;
    r.closing_end = end;
    return r;
  }
  if (s[k - 1] != ' ' && s[k - 1] != '\t') return r;
  r.closing_start = k;
  r.closing_end = end;
  std::size_t ce = k;
  while (ce > from && is_cmark_space(s[ce - 1])) --ce;
  r.content_end = ce;
  return r;
}

std::size_t scan_fence_open(std::string_view s) noexcept {
  if (s.empty() || (s[0] != '`' && s[0] != '~')) return 0;
  const char c = s[0];
  std::size_t n = 0;
  while (n < s.size() && s[n] == c) ++n;
  if (n < 3) return 0;
  if (c == '`' && s.find('`', n) != std::string_view::npos) return 0;
  return n;
}

std::size_t scan_fence_close(std::string_view s) noexcept {
  if (s.empty() || (s[0] != '`' && s[0] != '~')) return 0;
  const char c = s[0];
  std::size_t n = 0;
  while (n < s.size() && s[n] == c) ++n;
  if (n < 3) return 0;
  for (std::size_t i = n; i < s.size(); ++i)
    if (s[i] != ' ' && s[i] != '\t') return 0;
  return n;
}

std::uint8_t scan_setext_underline(std::string_view s, std::size_t* run_end) noexcept {
  if (s.empty() || (s[0] != '=' && s[0] != '-')) return 0;
  const char c = s[0];
  std::size_t n = 0;
  while (n < s.size() && s[n] == c) ++n;
  for (std::size_t i = n; i < s.size(); ++i)
    if (s[i] != ' ' && s[i] != '\t') return 0;
  *run_end = n;
  return c == '=' ? 1 : 2;
}

std::optional<ListMarker> scan_list_marker(std::string_view s, bool interrupts_paragraph) noexcept {
  if (s.empty()) return std::nullopt;
  auto rest_is_blank = [&](std::size_t p) {
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    return p >= s.size();
  };
  const char c = s[0];
  ListMarker m;
  if (c == '*' || c == '-' || c == '+') {
    if (s.size() > 1 && !is_cmark_space(s[1])) return std::nullopt;
    if (interrupts_paragraph && rest_is_blank(1)) return std::nullopt;
    m.ch = c;
    m.width = 1;
    return m;
  }
  if (!is_digit(c)) return std::nullopt;
  std::size_t p = 0;
  std::uint32_t number = 0;
  do {
    number = number * 10 + static_cast<std::uint32_t>(s[p] - '0');
    ++p;
  } while (p < 9 && p < s.size() && is_digit(s[p]));
  if (interrupts_paragraph && number != 1) return std::nullopt;
  if (p >= s.size() || (s[p] != '.' && s[p] != ')')) return std::nullopt;
  const char delim = s[p];
  ++p;
  if (p < s.size() && !is_cmark_space(s[p])) return std::nullopt;
  if (interrupts_paragraph && rest_is_blank(p)) return std::nullopt;
  m.ordered = true;
  m.ch = delim;
  m.number = number;
  m.width = p;
  return m;
}

std::uint8_t scan_html_block_start(std::string_view s) noexcept {
  if (s.size() < 2 || s[0] != '<') return 0;
  if (s.substr(0, 4) == "<!--") return 2;
  if (s.substr(0, 9) == "<![CDATA[") return 5;
  if (s[1] == '!' && s.size() > 2 && is_alpha(s[2])) return 4;
  if (s[1] == '?') return 3;
  std::size_t p = 1;
  while (p < s.size() && is_alnum(s[p])) ++p;
  const std::string_view name = s.substr(1, p - 1);
  if (iequals(name, "script") || iequals(name, "pre") || iequals(name, "textarea") || iequals(name, "style")) {
    if (p >= s.size() || is_html_space(s[p]) || s[p] == '>') return 1;
  }
  std::size_t q = 1;
  if (s[q] == '/') ++q;
  std::size_t e = q;
  while (e < s.size() && is_alnum(s[e])) ++e;
  if (e > q && is_block_tag_name(s.substr(q, e - q))) {
    if (e >= s.size() || is_html_space(s[e]) || s[e] == '>') return 6;
    if (s[e] == '/' && e + 1 < s.size() && s[e + 1] == '>') return 6;
  }
  return 0;
}

std::size_t scan_html_tag(std::string_view s) noexcept {
  if (s.empty()) return 0;
  if (s[0] == '/') {
    const std::size_t n = scan_tag_name(s, 1);
    if (n == 0) return 0;
    const std::size_t p = skip_html_spaces(s, 1 + n);
    return p < s.size() && s[p] == '>' ? p + 1 : 0;
  }
  const std::size_t n = scan_tag_name(s, 0);
  if (n == 0) return 0;
  std::size_t p = n;
  for (;;) {
    const std::size_t sp = skip_html_spaces(s, p);
    if (sp == p) break;
    const std::size_t name = scan_attribute_name(s, sp);
    if (name == 0) break;
    p = sp + name;
    std::size_t v = skip_html_spaces(s, p);
    if (v < s.size() && s[v] == '=') {
      v = skip_html_spaces(s, v + 1);
      const std::size_t ve = scan_attribute_value(s, v);
      if (ve > v) p = ve;
    }
  }
  p = skip_html_spaces(s, p);
  if (p < s.size() && s[p] == '/') ++p;
  return p < s.size() && s[p] == '>' ? p + 1 : 0;
}

std::uint8_t scan_html_block_start_7(std::string_view s) noexcept {
  if (s.empty() || s[0] != '<') return 0;
  const std::size_t n = scan_html_tag(s.substr(1));
  if (n == 0) return 0;
  for (std::size_t i = 1 + n; i < s.size(); ++i)
    if (s[i] != ' ' && s[i] != '\t' && s[i] != '\f') return 0;
  return 7;
}

bool html_block_ends_on_line(std::uint8_t type, std::string_view s) noexcept {
  switch (type) {
    case 1:
      return icontains(s, "</script>") || icontains(s, "</pre>") || icontains(s, "</textarea>") ||
             icontains(s, "</style>");
    case 2: return s.find("-->") != std::string_view::npos;
    case 3: return s.find("?>") != std::string_view::npos;
    case 4: return s.find('>') != std::string_view::npos;
    case 5: return s.find("]]>") != std::string_view::npos;
    default: return false;
  }
}

namespace {

constexpr bool is_table_space(char c) noexcept { return c == ' ' || c == '\t' || c == '\v' || c == '\f'; }

constexpr bool is_cell_trim_space(char c) noexcept { return c == ' ' || c == '\t'; }

}  

bool is_valid_utf8(std::string_view s) noexcept {
  for (std::size_t i = 0; i < s.size();) {
    if (static_cast<unsigned char>(s[i]) < 0x80) {
      ++i;
      continue;
    }
    const Decoded d = decode_utf8(s, i);
    if (d.len == 1) return false;
    i += d.len;
  }
  return true;
}

namespace {

template <class Push>
bool scan_row_tokens(std::string_view s, Push&& push, std::size_t& cells) noexcept {
  const std::size_t len = s.size();
  if (!is_valid_utf8(s)) return false;
  std::size_t offset = 0;
  if (offset < len && s[offset] == '|') {
    push(TableToken{true, offset, offset + 1});
    ++offset;
    while (offset < len && is_table_space(s[offset])) ++offset;
  }
  bool expect_more = true;
  while (offset < len && expect_more) {
    const std::size_t cell_start = offset;
    std::size_t i = offset;
    while (i < len) {
      if (s[i] == '\\' && i + 1 < len && s[i + 1] == '|') {
        i += 2;
        continue;
      }
      if (s[i] == '|') break;
      ++i;
    }
    const std::size_t cell_end = i;
    offset = cell_end;
    bool pipe_matched = false;
    std::size_t pipe_pos = 0;
    if (offset < len && s[offset] == '|') {
      pipe_matched = true;
      pipe_pos = offset;
      ++offset;
      while (offset < len && is_table_space(s[offset])) ++offset;
    }
    if (cell_end > cell_start || pipe_matched) {
      std::size_t ts = cell_start, te = cell_end;
      while (ts < te && is_cell_trim_space(s[ts])) ++ts;
      while (te > ts && is_cell_trim_space(s[te - 1])) --te;
      push(TableToken{false, ts, te});
      if (++cells > 65535) return false;
    }
    if (pipe_matched) push(TableToken{true, pipe_pos, pipe_pos + 1});
    expect_more = pipe_matched;
  }
  return offset == len && cells > 0;
}

}  

std::optional<TableRowScan> scan_table_row(std::string_view s) noexcept {
  TableRowScan row;
  if (!scan_row_tokens(s, [&](TableToken t) { row.tokens.push_back(t); }, row.cells)) return std::nullopt;
  return row;
}

bool is_table_row(std::string_view s) noexcept {
  std::size_t cells = 0;
  return scan_row_tokens(s, [](TableToken) {}, cells);
}

std::optional<TableDelimiterScan> scan_table_delimiter_row(std::string_view s) noexcept {
  bool dash = false;
  for (char c : s) {
    if (c == '-') dash = true;
    else if (c != '|' && c != ':' && !is_table_space(c)) return std::nullopt;
  }
  if (!dash) return std::nullopt;
  auto row = scan_table_row(s);
  if (!row) return std::nullopt;
  TableDelimiterScan out;
  out.align.reserve(row->cells);
  for (const TableToken& t : row->tokens) {
    if (t.pipe) continue;
    const std::string_view cell = s.substr(t.start, t.end - t.start);
    std::string_view check = cell;
    while (!check.empty() && is_table_space(check.front())) check.remove_prefix(1);
    while (!check.empty() && is_table_space(check.back())) check.remove_suffix(1);
    if (check.empty()) return std::nullopt;  
    std::size_t a = 0, b = check.size();
    if (check[a] == ':') ++a;
    if (b > a && check[b - 1] == ':') --b;
    if (b <= a) return std::nullopt;  
    for (std::size_t i = a; i < b; ++i)
      if (check[i] != '-') return std::nullopt;
    const bool left = !cell.empty() && cell.front() == ':';
    const bool right = cell.size() > (left ? 1u : 0u) && cell.back() == ':';
    out.align.push_back(left && right ? Alignment::center
                         : left        ? Alignment::left
                         : right       ? Alignment::right
                                       : Alignment::none);
  }
  out.row = std::move(*row);
  return out;
}

std::optional<TaskMarker> scan_task_marker(std::string_view line, std::size_t bullet, std::size_t after) noexcept {
  for (std::size_t i = 0; i < bullet; ++i)
    if (!is_table_space(line[i])) return std::nullopt;
  if (after + 3 >= line.size()) return std::nullopt;  
  const std::string_view box = line.substr(after, 3);
  if (box != "[ ]" && box != "[x]" && box != "[X]") return std::nullopt;
  if (!is_table_space(line[after + 3])) return std::nullopt;
  TaskMarker m;
  m.start = after;
  m.end = after + 3;
  m.checked = line.find("[x]") != std::string_view::npos || line.find("[X]") != std::string_view::npos;
  return m;
}

}  
