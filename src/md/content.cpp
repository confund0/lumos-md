#include "md/content.hpp"

#include <algorithm>

namespace lumos::md {

void ContentBuffer::clear() noexcept {
  text_.clear();
  segments_.clear();
  buffer_starts_.clear();
}

void ContentBuffer::append(std::size_t source_start, std::string_view bytes) {
  buffer_starts_.push_back(text_.size());
  segments_.push_back({source_start, source_start + bytes.size()});
  text_.append(bytes);
}

void ContentBuffer::trim_end() noexcept {
  std::size_t e = text_.size();
  while (e > 0 && (text_[e - 1] == ' ' || text_[e - 1] == '\t')) --e;
  if (e == text_.size()) return;
  text_.resize(e);
  while (!segments_.empty() && buffer_starts_.back() >= e) {
    segments_.pop_back();
    buffer_starts_.pop_back();
  }
  if (!segments_.empty()) {
    const std::size_t buf_start = buffer_starts_.back();
    segments_.back().end = segments_.back().start + (e - buf_start);
  }
}

namespace {

template <class Pred>
std::size_t count_true(const std::vector<std::size_t>& starts, std::size_t hint, Pred pred) {
  const std::size_t n = starts.size();
  const std::size_t* first = starts.data();
  if (hint >= n || !pred(starts[hint])) {
    return static_cast<std::size_t>(std::partition_point(first, first + std::min(hint, n), pred) - first);
  }
  std::size_t lo = hint + 1;  
  std::size_t probe = lo;
  std::size_t step = 1;
  while (probe < n && pred(starts[probe])) {
    lo = probe + 1;
    probe = lo + step;
    step *= 2;
  }
  const std::size_t hi = std::min(probe, n);
  return static_cast<std::size_t>(std::partition_point(first + lo, first + hi, pred) - first);
}

}  

std::size_t ContentBuffer::source_end(std::size_t buffer_offset) const noexcept {
  std::size_t seg = 0;
  return source_end(buffer_offset, seg);
}

std::size_t ContentBuffer::source_start(std::size_t buffer_offset) const noexcept {
  std::size_t seg = 0;
  return source_start(buffer_offset, seg);
}

std::size_t ContentBuffer::source_end(std::size_t buffer_offset, std::size_t& seg) const noexcept {
  if (segments_.empty()) return 0;
  const std::size_t c = count_true(buffer_starts_, seg, [&](std::size_t b) { return b < buffer_offset; });
  if (c == 0) {
    seg = 0;
    return segments_.front().start;
  }
  seg = c - 1;
  return segments_[seg].start + (buffer_offset - buffer_starts_[seg]);
}

std::size_t ContentBuffer::source_start(std::size_t buffer_offset, std::size_t& seg) const noexcept {
  if (segments_.empty()) return 0;
  const std::size_t c = count_true(buffer_starts_, seg, [&](std::size_t b) { return b <= buffer_offset; });
  seg = c == 0 ? 0 : c - 1;
  return segments_[seg].start + (buffer_offset - buffer_starts_[seg]);
}

void ContentBuffer::gaps(std::vector<Segment>& out) const {
  for (std::size_t i = 0; i + 1 < segments_.size(); ++i) {
    if (segments_[i].end < segments_[i + 1].start) out.push_back({segments_[i].end, segments_[i + 1].start});
  }
}

}  
