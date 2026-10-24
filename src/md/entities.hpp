#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lumos::md {

inline constexpr std::size_t max_entity_name = 31;

struct EntityValue {
  char32_t first = 0;
  char32_t second = 0;  
};

bool lookup_entity(std::string_view name, EntityValue& out) noexcept;

std::size_t scan_entity(std::string_view s, std::size_t i, std::string* decoded);

}  
