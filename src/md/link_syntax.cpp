#include "md/link_syntax.hpp"

#include "md/scanners.hpp"
#include "md/unicode.hpp"

namespace lumos::md {

namespace {

std::optional<std::size_t> scan_line_end(std::string_view s, std::size_t i) noexcept {
  if (i >= s.size()) return i;
  if (s[i] == '\r') return i + 1 < s.size() && s[i + 1] == '\n' ? i + 2 : i + 1;
  if (s[i] == '\n') return i + 1;
  return std::nullopt;
}

std::size_t skip_spaces_tabs(std::string_view s, std::size_t i) noexcept {
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  return i;
}

}  

std::optional<std::size_t> scan_link_label(std::string_view s, std::size_t i, bool* empty) noexcept {
  if (i >= s.size() || s[i] != '[') return std::nullopt;
  std::size_t j = i + 1;
  std::size_t length = 0;
  while (j < s.size() && s[j] != '[' && s[j] != ']') {
    if (s[j] == '\\') {
      ++j;
      ++length;
      if (length > max_label_bytes) return std::nullopt;
      if (j < s.size() && is_ascii_punctuation(s[j])) {
        ++j;
        ++length;
        if (length > max_label_bytes) return std::nullopt;
      }
    } else {
      ++j;
      ++length;
      if (length > max_label_bytes) return std::nullopt;
    }
  }
  if (j >= s.size() || s[j] != ']') return std::nullopt;
  std::size_t b = i + 1, e = j;
  while (b < e && is_cmark_space(s[b])) ++b;
  while (e > b && is_cmark_space(s[e - 1])) --e;
  if (b == e) {
    if (empty == nullptr) return std::nullopt;
    *empty = true;
    return j + 1;
  }
  if (empty != nullptr) *empty = false;
  return j + 1;
}

std::optional<Destination> scan_link_destination(std::string_view s, std::size_t i) noexcept {
  if (i < s.size() && s[i] == '<') {
    std::size_t j = i + 1;
    while (j < s.size()) {
      if (s[j] == '>') {
        Destination d;
        d.start = i;
        d.end = j + 1;
        d.pointy = true;
        return d;
      }
      if (s[j] == '\\') {
        if (j + 1 < s.size() && s[j + 1] != '\n' && s[j + 1] != '\r') {
          j += 2;
          continue;
        }
        return std::nullopt;
      }
      if (s[j] == '\n' || s[j] == '\r' || s[j] == '<') return std::nullopt;
      ++j;
    }
    return std::nullopt;  
  }

  std::size_t j = i;
  std::size_t depth = 0;
  while (j < s.size()) {
    const char c = s[j];
    if (c == '\\' && j + 1 < s.size() && is_ascii_punctuation(s[j + 1])) {
      j += 2;
      continue;
    }
    if (c == '(') {
      ++depth;
      if (depth > max_destination_parens) return std::nullopt;
      ++j;
      continue;
    }
    if (c == ')') {
      if (depth == 0) break;
      --depth;
      ++j;
      continue;
    }
    if (is_cmark_space(c)) {
      if (j == i) return std::nullopt;
      break;
    }
    ++j;
  }
  if (depth != 0 || j == i) return std::nullopt;
  Destination d;
  d.start = i;
  d.end = j;
  d.pointy = false;
  return d;
}

std::optional<std::size_t> scan_link_title(std::string_view s, std::size_t i) noexcept {
  if (i >= s.size()) return std::nullopt;
  char close;
  if (s[i] == '"') close = '"';
  else if (s[i] == '\'') close = '\'';
  else if (s[i] == '(') close = ')';
  else return std::nullopt;
  std::size_t j = i + 1;
  while (j < s.size()) {
    const char c = s[j];
    if (c == '\\' && j + 1 < s.size()) {
      j += 2;
      continue;
    }
    if (c == close) return j + 1;
    if (close == ')' && c == '(') return std::nullopt;
    ++j;
  }
  return std::nullopt;
}

std::size_t skip_link_whitespace(std::string_view s, std::size_t i, bool full, bool* saw_line_ending) noexcept {
  if (!full) {
    i = skip_spaces_tabs(s, i);
    const auto after = scan_line_end(s, i);
    const bool saw = after.has_value();
    if (saw) i = skip_spaces_tabs(s, *after);
    if (saw_line_ending) *saw_line_ending = saw;
    return i;
  }
  bool saw = false;
  while (i < s.size()) {
    const char c = s[i];
    if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
      ++i;
    } else if (c == '\r' || c == '\n') {
      saw = true;
      ++i;
    } else {
      break;
    }
  }
  if (saw_line_ending) *saw_line_ending = saw;
  return i;
}

std::optional<DefinitionSyntax> scan_link_reference_definition(std::string_view s) noexcept {
  const auto label_end = scan_link_label(s, 0);
  if (!label_end) return std::nullopt;
  const std::size_t label_start = 0;
  std::size_t pos = *label_end;
  const std::size_t label_end_pos = pos;
  if (pos >= s.size() || s[pos] != ':') return std::nullopt;
  const std::size_t colon = pos;
  ++pos;
  pos = skip_link_whitespace(s, pos, false);
  const auto dest = scan_link_destination(s, pos);
  if (!dest) return std::nullopt;

  const std::size_t before_title = dest->end;
  bool saw_le = false;
  const std::size_t after_spnl = skip_link_whitespace(s, before_title, false, &saw_le);
  std::size_t title_start = before_title, title_end = before_title;
  if (after_spnl != before_title) {
    if (const auto t = scan_link_title(s, after_spnl)) {
      title_start = after_spnl;
      title_end = *t;
    }
  }

  auto trailing_ok = [&](std::size_t p, std::size_t& end_pos) {
    const std::size_t q = skip_spaces_tabs(s, p);
    const auto after = scan_line_end(s, q);
    if (!after) return false;
    end_pos = *after;
    return true;
  };

  std::size_t end_pos = 0;
  if (title_end == before_title || !trailing_ok(title_end, end_pos)) {
    title_start = title_end = before_title;
    if (!trailing_ok(before_title, end_pos)) return std::nullopt;
  }

  DefinitionSyntax def;
  def.label_start = label_start;
  def.label_end = label_end_pos;
  def.colon = colon;
  def.destination = *dest;
  def.title_start = title_start;
  def.title_end = title_end;
  def.end = end_pos;
  return def;
}

}  
