#include "md/line_reader.hpp"

namespace lumos::md {

namespace {

std::size_t find_line_end(std::string_view s) noexcept {
  for (std::size_t i = 0; i < s.size(); ++i)
    if (s[i] == '\n' || s[i] == '\r') return i;
  return std::string_view::npos;
}

}  

std::string_view LineReader::chunk_for(std::size_t offset) {
  if (offset < chunk_start_ || offset >= chunk_start_ + chunk_.size()) {
    chunk_ = text_->chunk_at(offset);
    chunk_start_ = offset;
  }
  return chunk_.substr(offset - chunk_start_);
}

Line LineReader::read(std::size_t offset) {
  Line line;
  line.start = offset;
  const std::size_t size = text_->size();
  if (offset >= size) return line;
  auto ending_at = [&](std::size_t at, char c) -> std::uint8_t {
    if (c == '\n') return 1;
    if (at + 1 < size && chunk_for(at + 1).front() == '\n') return 2;
    return 1;
  };
  std::string_view c = chunk_for(offset);
  std::size_t k = find_line_end(c);
  if (k != std::string_view::npos) {
    const char e = c[k];
    line.text = c.substr(0, k);
    line.ending = ending_at(offset + k, e);
    return line;
  }
  buffer_.assign(c);
  std::size_t pos = offset + c.size();
  while (pos < size) {
    c = chunk_for(pos);
    k = find_line_end(c);
    if (k != std::string_view::npos) {
      buffer_.append(c.substr(0, k));
      line.ending = ending_at(pos + k, c[k]);
      break;
    }
    buffer_.append(c);
    pos += c.size();
  }
  line.text = buffer_;
  return line;
}

void LineCursor::find_first_nonspace() noexcept {
  if (fns_valid_ && fns_pos_ > pos_) return;
  std::size_t p = pos_;
  std::size_t col = column_;
  while (p < text_.size()) {
    const char c = text_[p];
    if (c == ' ') {
      ++col;
    } else if (c == '\t') {
      col += tab_stop - col % tab_stop;
    } else {
      break;
    }
    ++p;
  }
  fns_pos_ = p;
  fns_column_ = col;
  fns_valid_ = true;
}

void LineCursor::advance(std::size_t count, bool columns) noexcept {
  while (count > 0 && pos_ < text_.size()) {
    if (text_[pos_] == '\t') {
      const std::size_t to_tab = tab_stop - column_ % tab_stop;
      if (columns) {
        partial_ = to_tab > count;
        const std::size_t n = to_tab < count ? to_tab : count;
        column_ += n;
        if (!partial_) ++pos_;
        count -= n;
      } else {
        partial_ = false;
        column_ += to_tab;
        ++pos_;
        --count;
      }
    } else {
      partial_ = false;
      ++pos_;
      ++column_;
      --count;
    }
  }
}

}  
