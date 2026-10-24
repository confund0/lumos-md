#include "lumos/qt/text_map.hpp"

#include <algorithm>

namespace lumos::qt {
namespace {

struct Decoded {
  char32_t cp = 0;
  std::size_t length = 1;
  bool valid = false;
};

Decoded decode(std::string_view s, std::size_t i) {
  auto byte = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
  auto in = [](unsigned char c, unsigned lo, unsigned hi) { return c >= lo && c <= hi; };
  const unsigned char c = byte(i);
  if (c < 0x80U) return {c, 1, true};
  std::size_t need = 0;
  unsigned lo = 0x80U;
  unsigned hi = 0xBFU;
  char32_t cp = 0;
  if (in(c, 0xC2U, 0xDFU)) {
    need = 1;
    cp = c & 0x1FU;
  } else if (in(c, 0xE0U, 0xEFU)) {
    need = 2;
    cp = c & 0x0FU;
    if (c == 0xE0U) lo = 0xA0U;
    if (c == 0xEDU) hi = 0x9FU;
  } else if (in(c, 0xF0U, 0xF4U)) {
    need = 3;
    cp = c & 0x07U;
    if (c == 0xF0U) lo = 0x90U;
    if (c == 0xF4U) hi = 0x8FU;
  } else {
    return {};
  }
  if (s.size() - i - 1 < need) return {};
  if (!in(byte(i + 1), lo, hi)) return {};
  for (std::size_t k = 1; k <= need; ++k) {
    const unsigned char b = byte(i + k);
    if (k > 1 && !in(b, 0x80U, 0xBFU)) return {};
    cp = (cp << 6U) | (b & 0x3FU);
  }
  return {cp, need + 1, true};
}

}  

std::vector<LineSpan> split_lines(std::string_view text, std::size_t base) {
  std::vector<LineSpan> lines;
  std::size_t start = 0;
  while (true) {
    const std::size_t nl = text.find('\n', start);
    if (nl == std::string_view::npos) {
      lines.push_back(LineSpan{base + start, text.size() - start, 0});
      break;
    }
    const bool crlf = nl > start && text[nl - 1] == '\r';
    const std::size_t length = nl - start - (crlf ? 1 : 0);
    lines.push_back(LineSpan{base + start, length, crlf ? 2U : 1U});
    start = nl + 1;
  }
  return lines;
}

Utf16Map::Utf16Map(std::string_view utf8) : byte_size_(utf8.size()) {
  const bool ascii =
      std::all_of(utf8.begin(), utf8.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80U; });
  if (ascii) {
    text_ = QString::fromLatin1(utf8.data(), static_cast<qsizetype>(utf8.size()));
    return;
  }
  text_.reserve(static_cast<qsizetype>(utf8.size()));
  bytes_.reserve(utf8.size() + 1);
  std::size_t i = 0;
  while (i < utf8.size()) {
    const Decoded d = decode(utf8, i);
    if (!d.valid) {
      text_.append(QChar(QChar::ReplacementCharacter));
      bytes_.push_back(i);
      i += 1;
      continue;
    }
    if (d.cp >= 0x10000U) {
      text_.append(QChar(QChar::highSurrogate(d.cp)));
      text_.append(QChar(QChar::lowSurrogate(d.cp)));
      bytes_.push_back(i);
      bytes_.push_back(i);
    } else {
      text_.append(QChar(static_cast<char16_t>(d.cp)));
      bytes_.push_back(i);
    }
    i += d.length;
  }
  bytes_.push_back(utf8.size());
}

int Utf16Map::to_utf16(std::size_t byte) const {
  byte = std::min(byte, byte_size_);
  if (bytes_.empty()) return static_cast<int>(byte);
  const auto it = std::lower_bound(bytes_.begin(), bytes_.end(), byte);
  auto index = static_cast<std::size_t>(it - bytes_.begin());
  if (bytes_[index] != byte) {
    --index;
    if (index > 0 && bytes_[index - 1] == bytes_[index]) --index;
  }
  return static_cast<int>(index);
}

std::size_t Utf16Map::to_utf8(int pos) const {
  if (pos <= 0) return 0;
  const auto upos = static_cast<std::size_t>(pos);
  if (bytes_.empty()) return std::min(upos, byte_size_);
  return bytes_[std::min(upos, bytes_.size() - 1)];
}

}  
