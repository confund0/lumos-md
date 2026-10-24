#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace lumos::md {

inline constexpr std::size_t max_label_bytes = 1000;
std::optional<std::size_t> scan_link_label(std::string_view s, std::size_t i, bool* empty = nullptr) noexcept;

struct Destination {
  std::size_t start = 0;  
  std::size_t end = 0;
  bool pointy = false;    
};
inline constexpr std::size_t max_destination_parens = 32;
std::optional<Destination> scan_link_destination(std::string_view s, std::size_t i) noexcept;

std::optional<std::size_t> scan_link_title(std::string_view s, std::size_t i) noexcept;

std::size_t skip_link_whitespace(std::string_view s, std::size_t i, bool full, bool* saw_line_ending = nullptr) noexcept;

struct DefinitionSyntax {
  std::size_t label_start = 0, label_end = 0;  
  std::size_t colon = 0;
  Destination destination;
  std::size_t title_start = 0, title_end = 0;  
  std::size_t end = 0;  
};
std::optional<DefinitionSyntax> scan_link_reference_definition(std::string_view s) noexcept;

}  
