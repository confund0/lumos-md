
#include <algorithm>
#include <cstdint>
#include <stdexcept>

#include "lumos/log.hpp"
#include "lumos/text_source.hpp"
#include "lumos/types.hpp"

namespace lumos {
namespace {

constexpr std::string_view kComp = "text";

std::string num(std::size_t v) { return std::to_string(v); }

}  

TextRegion region_of(const Edit& e) noexcept {
  return TextRegion{e.offset, e.offset + e.removed, e.offset + e.inserted.size()};
}

TextRegion merge_edits(std::span<const Edit> edits) noexcept {
  TextRegion r;
  bool first = true;
  std::int64_t delta = 0;
  for (const Edit& e : edits) {
    const std::size_t old_end = e.offset + e.removed;
    const std::size_t new_end = e.offset + e.inserted.size();
    if (first) {
      r.start = e.offset;
      r.new_end = new_end;
      first = false;
    } else {
      r.new_end = r.new_end >= old_end ? r.new_end - e.removed + e.inserted.size() : new_end;
      r.start = std::min(r.start, e.offset);
    }
    delta += static_cast<std::int64_t>(e.inserted.size()) - static_cast<std::int64_t>(e.removed);
  }
  r.old_end = static_cast<std::size_t>(static_cast<std::int64_t>(r.new_end) - delta);
  return r;
}

StringSource::StringSource(std::string_view text) : text_(text) {
  starts_.push_back(0);
  for (std::size_t i = 0; i < text_.size(); ++i)
    if (text_[i] == '\n') starts_.push_back(i + 1);
}

std::string_view StringSource::chunk_at(std::size_t offset) const {
  if (offset > text_.size()) {
    log(LogLevel::debug, kComp, "chunk_out_of_range", {{"offset", num(offset)}, {"size", num(text_.size())}});
    throw std::out_of_range("StringSource::chunk_at: offset past end");
  }
  return text_.substr(offset);
}

std::string StringSource::slice(std::size_t offset, std::size_t length) const {
  if (offset > text_.size() || length > text_.size() - offset) {
    log(LogLevel::debug, kComp, "slice_out_of_range",
        {{"offset", num(offset)}, {"length", num(length)}, {"size", num(text_.size())}});
    throw std::out_of_range("StringSource::slice: range past end");
  }
  return std::string(text_.substr(offset, length));
}

std::size_t StringSource::line_of(std::size_t offset) const {
  if (offset > text_.size()) {
    log(LogLevel::debug, kComp, "line_of_out_of_range", {{"offset", num(offset)}, {"size", num(text_.size())}});
    throw std::out_of_range("StringSource::line_of: offset past end");
  }
  return static_cast<std::size_t>(std::upper_bound(starts_.begin(), starts_.end(), offset) - starts_.begin()) - 1;
}

std::size_t StringSource::line_start(std::size_t line) const {
  if (line >= starts_.size()) {
    log(LogLevel::debug, kComp, "line_out_of_range", {{"line", num(line)}, {"lines", num(starts_.size())}});
    throw std::out_of_range("StringSource::line_start: line past end");
  }
  return starts_[line];
}

}  
