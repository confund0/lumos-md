#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lumos::md {

struct Decoded {
  char32_t cp = 0;      
  std::uint8_t len = 0; 
};

Decoded decode_utf8(std::string_view s, std::size_t i) noexcept;

Decoded decode_utf8_before(std::string_view s, std::size_t i) noexcept;

void append_utf8(char32_t cp, std::string& out);

bool is_unicode_whitespace(char32_t cp) noexcept;

bool is_unicode_punctuation(char32_t cp) noexcept;

bool is_autolink_punctuation(char32_t cp) noexcept;

constexpr bool is_ascii_punctuation(char c) noexcept {
  return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

constexpr bool is_space_or_tab(char c) noexcept { return c == ' ' || c == '\t'; }

void append_case_fold(char32_t cp, std::string& out);

std::string_view unicode_category_version() noexcept;
std::string_view unicode_case_fold_version() noexcept;

}  
