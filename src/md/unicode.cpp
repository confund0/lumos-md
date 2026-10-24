#include "md/unicode.hpp"

#include <algorithm>
#include <span>

namespace lumos::md {

namespace {

struct CodeRange {
  char32_t first;
  char32_t last;
};

struct CaseFold {
  char32_t cp;
  char32_t to[3];  
};

#include "md/unicode_tables.inc"

bool in_ranges(std::span<const CodeRange> ranges, char32_t cp) noexcept {
  auto it = std::upper_bound(ranges.begin(), ranges.end(), cp,
                             [](char32_t c, const CodeRange& r) { return c < r.first; });
  if (it == ranges.begin()) return false;
  --it;
  return cp <= it->last;
}

constexpr bool is_cont(unsigned char c) noexcept { return (c & 0xC0) == 0x80; }

}  

Decoded decode_utf8(std::string_view s, std::size_t i) noexcept {
  if (i >= s.size()) return {0, 0};
  const auto b0 = static_cast<unsigned char>(s[i]);
  if (b0 < 0x80) return {b0, 1};
  const std::size_t left = s.size() - i;
  auto at = [&](std::size_t k) { return static_cast<unsigned char>(s[i + k]); };
  if (b0 >= 0xC2 && b0 <= 0xDF) {
    if (left >= 2 && is_cont(at(1))) return {static_cast<char32_t>(((b0 & 0x1Fu) << 6) | (at(1) & 0x3Fu)), 2};
  } else if (b0 >= 0xE0 && b0 <= 0xEF) {
    if (left >= 3 && is_cont(at(1)) && is_cont(at(2))) {
      const char32_t cp = ((b0 & 0x0Fu) << 12) | ((at(1) & 0x3Fu) << 6) | (at(2) & 0x3Fu);
      if (cp >= 0x800 && (cp < 0xD800 || cp > 0xDFFF)) return {cp, 3};
    }
  } else if (b0 >= 0xF0 && b0 <= 0xF4) {
    if (left >= 4 && is_cont(at(1)) && is_cont(at(2)) && is_cont(at(3))) {
      const char32_t cp =
          ((b0 & 0x07u) << 18) | ((at(1) & 0x3Fu) << 12) | ((at(2) & 0x3Fu) << 6) | (at(3) & 0x3Fu);
      if (cp >= 0x10000 && cp <= 0x10FFFF) return {cp, 4};
    }
  }
  return {0xFFFD, 1};
}

Decoded decode_utf8_before(std::string_view s, std::size_t i) noexcept {
  if (i == 0 || i > s.size()) return {0, 0};
  for (std::size_t len = 1; len <= 4 && len <= i; ++len) {
    const std::size_t start = i - len;
    if (is_cont(static_cast<unsigned char>(s[start]))) continue;
    const Decoded d = decode_utf8(s, start);
    if (d.len == len) return d;
    break;
  }
  return {0xFFFD, 1};
}

void append_utf8(char32_t cp, std::string& out) {
  if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) cp = 0xFFFD;
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

bool is_unicode_whitespace(char32_t cp) noexcept {
  if (cp == '\t' || cp == '\n' || cp == '\f' || cp == '\r') return true;
  if (cp < 0x80) return cp == ' ';
  return in_ranges(space_ranges, cp);
}

bool is_unicode_punctuation(char32_t cp) noexcept {
  if (cp < 0x80) return is_ascii_punctuation(static_cast<char>(cp));
  return in_ranges(punctuation_ranges, cp);
}

bool is_autolink_punctuation(char32_t cp) noexcept {
  if (cp < 0x80) return is_ascii_punctuation(static_cast<char>(cp));
  return in_ranges(punctuation_p_ranges, cp);
}

void append_case_fold(char32_t cp, std::string& out) {
  if (cp < 0x80) {
    out += static_cast<char>(cp >= 'A' && cp <= 'Z' ? cp + 32 : cp);
    return;
  }
  const std::span<const CaseFold> folds(case_folds);
  auto it = std::lower_bound(folds.begin(), folds.end(), cp, [](const CaseFold& f, char32_t c) { return f.cp < c; });
  if (it == folds.end() || it->cp != cp) {
    append_utf8(cp, out);
    return;
  }
  for (char32_t c : it->to)
    if (c != 0) append_utf8(c, out);
}

std::string_view unicode_category_version() noexcept { return LUMOS_UNICODE_CATEGORY_VERSION; }
std::string_view unicode_case_fold_version() noexcept { return LUMOS_UNICODE_CASE_FOLD_VERSION; }

}  
