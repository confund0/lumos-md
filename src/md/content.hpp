#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lumos::md {

struct Segment {
  std::size_t start = 0;
  std::size_t end = 0;
  std::size_t size() const noexcept { return end - start; }
};

class ContentBuffer {
 public:
  void clear() noexcept;
  void append(std::size_t source_start, std::string_view bytes);
  void trim_end() noexcept;

  std::string_view text() const noexcept { return text_; }
  std::span<const Segment> segments() const noexcept { return segments_; }

  std::size_t source_start(std::size_t buffer_offset) const noexcept;
  std::size_t source_end(std::size_t buffer_offset) const noexcept;
  std::size_t source_start(std::size_t buffer_offset, std::size_t& seg) const noexcept;
  std::size_t source_end(std::size_t buffer_offset, std::size_t& seg) const noexcept;

  void gaps(std::vector<Segment>& out) const;

 private:
  std::string text_;
  std::vector<Segment> segments_;         
  std::vector<std::size_t> buffer_starts_; 
};

}  
