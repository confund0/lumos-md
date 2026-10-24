#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace lumos {

struct Edit {
  std::size_t offset = 0;
  std::size_t removed = 0;  
  std::string inserted;     
};

struct ByteRange {
  std::size_t start = 0;
  std::size_t end = 0;

  bool operator==(const ByteRange&) const = default;
};

struct TextRegion {
  std::size_t start = 0;
  std::size_t old_end = 0;
  std::size_t new_end = 0;

  bool operator==(const TextRegion&) const = default;
};

TextRegion region_of(const Edit& e) noexcept;

TextRegion merge_edits(std::span<const Edit> edits) noexcept;

}  
