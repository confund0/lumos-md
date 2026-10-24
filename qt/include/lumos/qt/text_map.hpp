#pragma once

#include <QString>

#include <cstddef>
#include <string_view>
#include <vector>

namespace lumos::qt {

struct LineSpan {
  std::size_t start = 0;  
  std::size_t length = 0;  
  std::size_t ending = 0;  

  std::size_t text_end() const { return start + length; }
  std::size_t next_start() const { return start + length + ending; }
  bool operator==(const LineSpan&) const = default;
};

std::vector<LineSpan> split_lines(std::string_view text, std::size_t base = 0);

class Utf16Map {
 public:
  Utf16Map() = default;
  explicit Utf16Map(std::string_view utf8);

  const QString& text() const { return text_; }
  std::size_t byte_size() const { return byte_size_; }
  int size() const { return static_cast<int>(text_.size()); }

  int to_utf16(std::size_t byte) const;
  std::size_t to_utf8(int pos) const;

 private:
  QString text_;
  std::size_t byte_size_ = 0;
  std::vector<std::size_t> bytes_;
};

}  
