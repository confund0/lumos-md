#include "md/entities.hpp"

#include <algorithm>
#include <span>

#include "md/unicode.hpp"

namespace lumos::md {

namespace {

struct EntityEntry {
  std::string_view name;
  char32_t first;
  char32_t second;
};

#include "md/entity_table.inc"

constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
constexpr bool is_hex(char c) noexcept { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
constexpr bool is_alnum(char c) noexcept {
  return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
constexpr std::uint32_t hex_value(char c) noexcept {
  if (is_digit(c)) return static_cast<std::uint32_t>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<std::uint32_t>(c - 'a' + 10);
  return static_cast<std::uint32_t>(c - 'A' + 10);
}

}  

bool lookup_entity(std::string_view name, EntityValue& out) noexcept {
  const std::span<const EntityEntry> table(entity_table);
  auto it = std::lower_bound(table.begin(), table.end(), name,
                             [](const EntityEntry& e, std::string_view n) { return e.name < n; });
  if (it == table.end() || it->name != name) return false;
  out.first = it->first;
  out.second = it->second;
  return true;
}

std::size_t scan_entity(std::string_view s, std::size_t i, std::string* decoded) {
  if (i >= s.size() || s[i] != '&') return 0;
  std::size_t p = i + 1;
  if (p < s.size() && s[p] == '#') {
    ++p;
    const bool hex = p < s.size() && (s[p] == 'x' || s[p] == 'X');
    if (hex) ++p;
    const std::size_t digits_start = p;
    const std::size_t max_digits = hex ? 6 : 7;
    std::uint32_t cp = 0;
    while (p < s.size() && (hex ? is_hex(s[p]) : is_digit(s[p])) && p - digits_start < max_digits) {
      cp = cp * (hex ? 16 : 10) + hex_value(s[p]);
      ++p;
    }
    if (p == digits_start || p >= s.size() || s[p] != ';') return 0;
    if (decoded) {
      if (cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) cp = 0xFFFD;
      append_utf8(cp, *decoded);
    }
    return p + 1 - i;
  }
  const std::size_t name_start = p;
  while (p < s.size() && is_alnum(s[p]) && p - name_start <= max_entity_name) ++p;
  if (p == name_start || p >= s.size() || s[p] != ';') return 0;
  EntityValue v;
  if (!lookup_entity(s.substr(name_start, p - name_start), v)) return 0;
  if (decoded) {
    append_utf8(v.first, *decoded);
    if (v.second != 0) append_utf8(v.second, *decoded);
  }
  return p + 1 - i;
}

}  
