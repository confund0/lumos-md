#include "lumos/commands.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

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

void log_regex_error(std::string_view event, int code) {
  if (log_enabled(LogLevel::debug)) log(LogLevel::debug, "commands", event, {{"code", std::to_string(code)}});
}

void log_replace(std::string_view event, std::size_t edits) {
  if (log_enabled(LogLevel::debug)) log(LogLevel::debug, "commands", event, {{"edits", std::to_string(edits)}});
}

constexpr std::uint32_t match_limit = 10'000'000;
constexpr std::uint32_t heap_limit_kib = 64 * 1024;

struct CodeFree {
  void operator()(pcre2_code* p) const { pcre2_code_free(p); }
};
struct MatchDataFree {
  void operator()(pcre2_match_data* p) const { pcre2_match_data_free(p); }
};
struct MatchContextFree {
  void operator()(pcre2_match_context* p) const { pcre2_match_context_free(p); }
};

class Regex {
 public:
  static std::optional<Regex> compile(std::string_view pattern, bool case_sensitive) {
    std::uint32_t flags = PCRE2_UTF | PCRE2_UCP;
    if (!case_sensitive) flags |= PCRE2_CASELESS;
    int code = 0;
    PCRE2_SIZE offset = 0;
    Regex re;
    re.code_.reset(pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.data()), pattern.size(), flags, &code,
                                 &offset, nullptr));
    if (!re.code_) {
      log_regex_error("find_invalid_regex", code);
      return std::nullopt;
    }
    re.data_.reset(pcre2_match_data_create_from_pattern(re.code_.get(), nullptr));
    re.context_.reset(pcre2_match_context_create(nullptr));
    if (!re.data_ || !re.context_) return std::nullopt;
    pcre2_set_match_limit(re.context_.get(), match_limit);
    pcre2_set_heap_limit(re.context_.get(), heap_limit_kib);
    return re;
  }

  enum class Result { hit, none, error };

  Result match(std::string_view piece, std::size_t from, std::uint32_t options) {
    const int rc = pcre2_match(code_.get(), reinterpret_cast<PCRE2_SPTR>(piece.data()), piece.size(), from,
                               options | PCRE2_NO_UTF_CHECK, data_.get(), context_.get());
    if (rc == PCRE2_ERROR_NOMATCH) return Result::none;
    if (rc < 0) {
      log_regex_error("find_regex_failed", rc);
      return Result::error;
    }
    return Result::hit;
  }

  std::optional<ByteRange> group(std::uint32_t n) const {
    if (n >= pcre2_get_ovector_count(data_.get())) return std::nullopt;
    const PCRE2_SIZE* ov = pcre2_get_ovector_pointer(data_.get());
    if (ov[2 * n] == PCRE2_UNSET) return std::nullopt;
    return ByteRange{ov[2 * n], ov[2 * n + 1]};
  }

 private:
  Regex() = default;
  std::unique_ptr<pcre2_code, CodeFree> code_;
  std::unique_ptr<pcre2_match_data, MatchDataFree> data_;
  std::unique_ptr<pcre2_match_context, MatchContextFree> context_;
};

void append_line_subjects(std::string_view text, ByteRange line, std::vector<ByteRange>& out) {
  std::size_t start = line.start;
  std::size_t i = line.start;
  while (i < line.end) {
    const Decoded d = md::decode_utf8(text.substr(0, line.end), i);
    if (d.cp == 0xFFFD && d.len == 1) {
      out.push_back({start, i});
      start = i + 1;
    }
    i += d.len;
  }
  out.push_back({start, line.end});
}

ByteRange line_at(std::string_view text, std::size_t pos) {
  const std::size_t before = pos == 0 ? std::string_view::npos : text.rfind('\n', pos - 1);
  const std::size_t start = before == std::string_view::npos ? 0 : before + 1;
  std::size_t end = text.find('\n', pos);
  if (end == std::string_view::npos) return {start, text.size()};
  if (end > start && text[end - 1] == '\r') --end;
  return {start, end};
}

std::vector<ByteRange> subjects(std::string_view text) {
  std::vector<ByteRange> out;
  std::size_t pos = 0;
  while (pos < text.size()) {
    const ByteRange line = line_at(text, pos);
    append_line_subjects(text, line, out);
    const std::size_t next = text.find('\n', line.end);
    if (next == std::string_view::npos) break;
    pos = next + 1;
  }
  return out;
}

std::string_view piece_text(std::string_view text, ByteRange piece) {
  return text.substr(piece.start, piece.end - piece.start);
}

std::size_t step_at(std::string_view text, std::size_t pos) {
  const Decoded d = md::decode_utf8(text, pos);
  return d.len != 0 ? d.len : 1;
}

template <typename Fn>
bool for_each_regex_hit(std::string_view text, Regex& re, bool whole_word, Fn&& fn) {
  for (const ByteRange piece : subjects(text)) {
    const std::string_view sub = piece_text(text, piece);
    std::size_t pos = 0;
    std::uint32_t options = 0;
    while (pos <= sub.size()) {
      const Regex::Result r = re.match(sub, pos, options);
      if (r == Regex::Result::error) return false;
      if (r == Regex::Result::none) {
        if ((options & PCRE2_ANCHORED) == 0 || pos == sub.size()) break;
        pos += step_at(sub, pos);
        options = 0;
        continue;
      }
      const ByteRange m = *re.group(0);
      const ByteRange hit{piece.start + m.start, piece.start + m.end};
      if (!whole_word || whole_word_ok(text, hit.start, hit.end)) fn(hit, sub);
      pos = m.end;
      options = m.start == m.end ? PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED : PCRE2_NOTEMPTY_ATSTART;
    }
  }
  return true;
}

std::optional<std::vector<ByteRange>> find_regex(std::string_view text, std::string_view pattern,
                                                   bool case_sensitive, bool whole_word) {
  auto re = Regex::compile(pattern, case_sensitive);
  if (!re) return std::nullopt;
  std::vector<ByteRange> hits;
  if (!for_each_regex_hit(text, *re, whole_word, [&](ByteRange hit, std::string_view) { hits.push_back(hit); }))
    return std::nullopt;
  return hits;
}

struct ReplacementPart {
  std::string text;
  std::optional<std::uint32_t> group;
};

std::vector<ReplacementPart> parse_replacement(std::string_view replacement) {
  std::vector<ReplacementPart> parts(1);
  std::size_t i = 0;
  while (i < replacement.size()) {
    const char c = replacement[i];
    const char next = i + 1 < replacement.size() ? replacement[i + 1] : '\0';
    if (c == '$' && next == '$') {
      parts.back().text += '$';
      i += 2;
    } else if (c == '$' && next == '&') {
      parts.back().group = 0;
      parts.emplace_back();
      i += 2;
    } else if (c == '$' && next >= '0' && next <= '9') {
      std::uint32_t n = static_cast<std::uint32_t>(next - '0');
      i += 2;
      if (i < replacement.size() && replacement[i] >= '0' && replacement[i] <= '9') {
        n = n * 10 + static_cast<std::uint32_t>(replacement[i] - '0');
        ++i;
      }
      parts.back().group = n;
      parts.emplace_back();
    } else {
      parts.back().text += c;
      ++i;
    }
  }
  return parts;
}

std::string expand(std::string_view piece, const Regex& re, const std::vector<ReplacementPart>& parts) {
  std::string out;
  for (const ReplacementPart& part : parts) {
    out += part.text;
    if (!part.group) continue;
    if (const auto g = re.group(*part.group)) out.append(piece.substr(g->start, g->end - g->start));
  }
  return out;
}

std::optional<std::string> expand_at(std::string_view text, ByteRange match, std::string_view pattern,
                                      std::string_view replacement, FindOptions options) {
  const std::string_view slice = text.substr(match.start, match.end - match.start);
  if (!options.regex) {
    if (options.case_sensitive ? slice != pattern : fold_plain(slice) != fold_plain(pattern)) return std::nullopt;
    return std::string(replacement);
  }
  auto re = Regex::compile(pattern, options.case_sensitive);
  if (!re) return std::nullopt;
  std::vector<ByteRange> pieces;
  append_line_subjects(text, line_at(text, match.start), pieces);
  const auto piece = std::find_if(pieces.begin(), pieces.end(),
                                  [&](const ByteRange& p) { return p.start <= match.start && match.end <= p.end; });
  if (piece == pieces.end()) return std::nullopt;
  const std::string_view sub = piece_text(text, *piece);
  const std::size_t start = match.start - piece->start;
  const std::size_t end = match.end - piece->start;
  for (const std::uint32_t o : {PCRE2_ANCHORED, PCRE2_ANCHORED | PCRE2_NOTEMPTY_ATSTART}) {
    if (re->match(sub, start, o) != Regex::Result::hit) return std::nullopt;
    const ByteRange m = *re->group(0);
    if (m.end == end) return expand(sub, *re, parse_replacement(replacement));
    if (m.start != m.end) return std::nullopt;
  }
  return std::nullopt;
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
  if (pattern.empty()) return std::nullopt;
  const std::string text = doc.text();
  std::vector<std::pair<ByteRange, std::string>> matches;
  if (options.regex) {
    auto re = Regex::compile(pattern, options.case_sensitive);
    if (!re) return std::nullopt;
    const std::vector<ReplacementPart> parts = parse_replacement(replacement);
    const bool ok = for_each_regex_hit(text, *re, options.whole_word, [&](ByteRange hit, std::string_view sub) {
      matches.emplace_back(hit, expand(sub, *re, parts));
    });
    if (!ok) return std::nullopt;
  } else {
    for (const ByteRange hit : find_literal(text, pattern, options.case_sensitive, options.whole_word))
      matches.emplace_back(hit, std::string(replacement));
  }
  if (matches.empty()) return std::nullopt;
  CommandResult r;
  std::size_t selection_pos = 0;
  bool first = true;
  for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
    const ByteRange& hit = it->first;
    const std::string& expanded = it->second;
    const std::size_t len = hit.end - hit.start;
    r.edits.push_back(Edit{hit.start, len, expanded});
    if (first) {
      selection_pos = hit.start + expanded.size();
      first = false;
    } else {
      selection_pos = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(selection_pos) +
                                               static_cast<std::ptrdiff_t>(expanded.size()) -
                                               static_cast<std::ptrdiff_t>(len));
    }
  }
  r.selection = Selection{selection_pos, selection_pos};
  log_replace("replace_all", r.edits.size());
  return r;
}

}  
