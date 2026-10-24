#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace lumos {

class TextSource {
 public:
  virtual ~TextSource() = default;

  virtual std::size_t size() const = 0;
  virtual std::string_view chunk_at(std::size_t offset) const = 0;
  virtual std::string slice(std::size_t offset, std::size_t length) const = 0;
  virtual std::size_t line_of(std::size_t offset) const = 0;
  virtual std::size_t line_start(std::size_t line) const = 0;
  virtual std::size_t line_count() const = 0;

 protected:
  TextSource() = default;
  TextSource(const TextSource&) = default;
  TextSource(TextSource&&) = default;
  TextSource& operator=(const TextSource&) = default;
  TextSource& operator=(TextSource&&) = default;
};

class StringSource final : public TextSource {
 public:
  explicit StringSource(std::string_view text);

  std::size_t size() const override { return text_.size(); }
  std::string_view chunk_at(std::size_t offset) const override;
  std::string slice(std::size_t offset, std::size_t length) const override;
  std::size_t line_of(std::size_t offset) const override;
  std::size_t line_start(std::size_t line) const override;
  std::size_t line_count() const override { return starts_.size(); }

 private:
  std::string_view text_;
  std::vector<std::size_t> starts_;  
};

}  
