#include "lumos/commands.hpp"

#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "lumos/log.hpp"
#include "md/unicode.hpp"

namespace lumos {
namespace {

using md::Decoded;

bool is_word_cp(char32_t cp) { return !(md::is_unicode_whitespace(cp) || md::is_unicode_punctuation(cp)); }

bool whole_word_ok(std::string_view text, std::size_t start, std::size_t end) {
  if (start > 0) {
    const Decoded d = md::decode_utf8_before(text, start);
    if (d.len != 0 && is_word_cp(d.cp)) return false;
  }
  if (end < text.size()) {
    const Decoded d = md::decode_utf8(text, end);
    if (d.len != 0 && is_word_cp(d.cp)) return false;
  }
  return true;
}

std::string fold_plain(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  std::size_t i = 0;
  while (i < s.size()) {
    const Decoded d = md::decode_utf8(s, i);
    md::append_case_fold(d.cp, out);
    i += d.len != 0 ? d.len : 1;
  }
  return out;
}

struct Folded {
  std::string text;
  std::vector<std::size_t> origin;
  std::vector<bool> boundary;
};

Folded fold_with_origin(std::string_view s) {
  Folded f;
  f.text.reserve(s.size());
  std::size_t i = 0;
  while (i < s.size()) {
    const Decoded d = md::decode_utf8(s, i);
    f.boundary.resize(f.text.size() + 1, false);
    f.boundary[f.text.size()] = true;
    md::append_case_fold(d.cp, f.text);
    f.origin.resize(f.text.size(), i);
    i += d.len != 0 ? d.len : 1;
  }
  f.boundary.resize(f.text.size() + 1, false);
  f.boundary[f.text.size()] = true;
  return f;
}

std::vector<ByteRange> find_literal(std::string_view text, std::string_view pattern, bool case_sensitive,
                                     bool whole_word) {
  std::vector<ByteRange> hits;
  if (pattern.empty()) return hits;
  if (case_sensitive) {
    std::size_t pos = 0;
    while (pos <= text.size()) {
      const std::size_t at = text.find(pattern, pos);
      if (at == std::string_view::npos) break;
      const std::size_t end = at + pattern.size();
      if (!whole_word || whole_word_ok(text, at, end)) hits.push_back({at, end});
      pos = end;
    }
    return hits;
  }
  const Folded folded = fold_with_origin(text);
  const std::string needle = fold_plain(pattern);
  if (needle.empty()) return hits;
  std::size_t pos = 0;
  while (pos <= folded.text.size()) {
    const std::size_t at = folded.text.find(needle, pos);
    if (at == std::string::npos) break;
    const std::size_t fend = at + needle.size();
    if (!folded.boundary[at] || !folded.boundary[fend]) {
      pos = at + 1;
      continue;
    }
    const std::size_t start = folded.origin[at];
    const std::size_t end = fend < folded.text.size() ? folded.origin[fend] : text.size();
    if (!whole_word || whole_word_ok(text, start, end)) hits.push_back({start, end});
    pos = fend;
  }
  return hits;
}

void log_invalid_regex() {
  if (log_enabled(LogLevel::debug)) log(LogLevel::debug, "commands", "find_invalid_regex");
}

void log_replace(std::string_view event, std::size_t edits) {
  if (log_enabled(LogLevel::debug)) log(LogLevel::debug, "commands", event, {{"edits", std::to_string(edits)}});
}

std::optional<std::regex> compile(std::string_view pattern, bool case_sensitive) {
  try {
    auto flags = std::regex::ECMAScript;
    if (!case_sensitive) flags |= std::regex::icase;
    return std::regex(std::string(pattern), flags);
  } catch (const std::regex_error&) {
    log_invalid_regex();
    return std::nullopt;
  }
}

std::optional<std::vector<ByteRange>> find_regex(const std::string& text, std::string_view pattern,
                                                   bool case_sensitive, bool whole_word) {
  const auto re = compile(pattern, case_sensitive);
  if (!re) return std::nullopt;
  std::vector<ByteRange> hits;
  for (auto it = std::sregex_iterator(text.begin(), text.end(), *re); it != std::sregex_iterator(); ++it) {
    const std::size_t start = static_cast<std::size_t>(it->position(0));
    const std::size_t end = start + static_cast<std::size_t>(it->length(0));
    if (!whole_word || whole_word_ok(text, start, end)) hits.push_back({start, end});
  }
  return hits;
}

std::optional<std::string> expand_at(const std::string& text, ByteRange match, std::string_view pattern,
                                      std::string_view replacement, FindOptions options) {
  const std::string slice = text.substr(match.start, match.end - match.start);
  if (!options.regex) {
    if (options.case_sensitive ? slice != pattern : fold_plain(slice) != fold_plain(pattern)) return std::nullopt;
    return std::string(replacement);
  }
  const auto re = compile(pattern, options.case_sensitive);
  if (!re || !std::regex_match(slice, *re)) return std::nullopt;
  return std::regex_replace(slice, *re, std::string(replacement), std::regex_constants::format_first_only);
}

}  

std::optional<std::vector<ByteRange>> find_all(const Document& doc, std::string_view pattern, FindOptions options) {
  if (pattern.empty()) return std::vector<ByteRange>{};
  const std::string text = doc.text();
  if (options.regex) return find_regex(text, pattern, options.case_sensitive, options.whole_word);
  return find_literal(text, pattern, options.case_sensitive, options.whole_word);
}

std::optional<CommandResult> replace_one(const Document& doc, ByteRange match, std::string_view pattern,
                                         std::string_view replacement, FindOptions options) {
  const std::string text = doc.text();
  if (match.start > match.end || match.end > text.size()) return std::nullopt;
  const auto expanded = expand_at(text, match, pattern, replacement, options);
  if (!expanded) return std::nullopt;
  CommandResult r;
  r.edits.push_back(Edit{match.start, match.end - match.start, *expanded});
  const std::size_t pos = match.start + expanded->size();
  r.selection = Selection{pos, pos};
  log_replace("replace_one", r.edits.size());
  return r;
}

std::optional<CommandResult> replace_all(const Document& doc, std::string_view pattern, std::string_view replacement,
                                         FindOptions options) {
  const auto matches = find_all(doc, pattern, options);
  if (!matches || matches->empty()) return std::nullopt;
  const std::string text = doc.text();
  CommandResult r;
  std::size_t selection_pos = 0;
  bool first = true;
  for (auto it = matches->rbegin(); it != matches->rend(); ++it) {
    const auto expanded = expand_at(text, *it, pattern, replacement, options);
    if (!expanded) continue;  
    const std::size_t len = it->end - it->start;
    r.edits.push_back(Edit{it->start, len, *expanded});
    if (first) {
      selection_pos = it->start + expanded->size();
      first = false;
    } else {
      selection_pos = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(selection_pos) +
                                               static_cast<std::ptrdiff_t>(expanded->size()) -
                                               static_cast<std::ptrdiff_t>(len));
    }
  }
  if (r.edits.empty()) return std::nullopt;
  r.selection = Selection{selection_pos, selection_pos};
  log_replace("replace_all", r.edits.size());
  return r;
}

}  
