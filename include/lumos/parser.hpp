#pragma once

#include <memory>
#include <span>
#include <string_view>

#include "lumos/syntax.hpp"
#include "lumos/text_source.hpp"
#include "lumos/types.hpp"

namespace lumos {

struct Dialect {
  bool strikethrough = true;
  bool task_lists = true;
  bool math = false;
  bool tables = true;
  bool extended_autolinks = true;
  bool front_matter = true;

  static constexpr Dialect commonmark() noexcept { return {false, false, false, false, false, false}; }

  bool operator==(const Dialect&) const = default;
};

class Parser {
 public:
  virtual ~Parser();
  Parser(const Parser&) = delete;
  Parser& operator=(const Parser&) = delete;

  virtual void parse(const TextSource& text) = 0;
  virtual void apply_edit(const TextRegion& region, const TextSource& text) = 0;
  virtual const SyntaxTree& tree() const = 0;
  virtual const Dialect& dialect() const = 0;
  virtual std::span<const TextRegion> changes() const = 0;
  virtual void wipe() = 0;

  void parse(std::string_view text);
  void apply_edit(const Edit& edit, std::string_view new_text);

 protected:
  Parser() = default;
};

std::unique_ptr<Parser> make_parser(const Dialect& dialect = {});

namespace md {
std::unique_ptr<Parser> make_markdown_parser(const Dialect& dialect = {});
}  

}  
