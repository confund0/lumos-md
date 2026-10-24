#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "lumos/text_source.hpp"

namespace lumos::md {

struct Line {
  std::size_t start = 0;    
  std::string_view text;    
  std::uint8_t ending = 0;  

  std::size_t content_end() const noexcept { return start + text.size(); }
  std::size_t end() const noexcept { return start + text.size() + ending; }
};

class LineReader {
 public:
  explicit LineReader(const TextSource& text) noexcept : text_(&text) {}

  Line read(std::size_t offset);

  std::size_t size() const { return text_->size(); }

 private:
  std::string_view chunk_for(std::size_t offset);

  const TextSource* text_;
  std::size_t chunk_start_ = 0;
  std::string_view chunk_;
  std::string buffer_;  
};

inline constexpr std::size_t tab_stop = 4;

class LineCursor {
 public:
  explicit LineCursor(std::string_view line) noexcept : text_(line) {}

  std::string_view text() const noexcept { return text_; }
  std::size_t pos() const noexcept { return pos_; }
  std::size_t column() const noexcept { return column_; }
  bool partial() const noexcept { return partial_; }
  std::size_t partial_rest() const noexcept { return partial_ ? tab_stop - column_ % tab_stop : 0; }
  std::size_t content_pos() const noexcept { return partial_ ? pos_ + 1 : pos_; }

  bool at_end() const noexcept { return pos_ >= text_.size(); }
  char peek() const noexcept { return pos_ < text_.size() ? text_[pos_] : '\0'; }
  char at(std::size_t i) const noexcept { return i < text_.size() ? text_[i] : '\0'; }

  std::size_t first_nonspace() noexcept {
    find_first_nonspace();
    return fns_pos_;
  }
  std::size_t indent() noexcept {
    find_first_nonspace();
    return fns_column_ - column_;
  }
  bool blank() noexcept {
    find_first_nonspace();
    return fns_pos_ >= text_.size();
  }

  void advance(std::size_t count, bool columns) noexcept;
  void skip_to_first_nonspace() noexcept { advance(first_nonspace() - pos_, false); }

  struct Mark {
    std::size_t pos, column;
    bool partial;
  };
  Mark mark() const noexcept { return {pos_, column_, partial_}; }
  void reset(const Mark& m) noexcept {
    pos_ = m.pos;
    column_ = m.column;
    partial_ = m.partial;
  }

 private:
  void find_first_nonspace() noexcept;

  std::string_view text_;
  std::size_t pos_ = 0;
  std::size_t column_ = 0;
  bool partial_ = false;
  std::size_t fns_pos_ = 0;
  std::size_t fns_column_ = 0;
  bool fns_valid_ = false;
};

}  
