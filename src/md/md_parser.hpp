#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "lumos/parser.hpp"
#include "lumos/syntax.hpp"
#include "lumos/text_source.hpp"
#include "lumos/types.hpp"
#include "md/block_state.hpp"
#include "md/features.hpp"
#include "md/ref_index.hpp"

namespace lumos::md {

class MarkdownParser final : public Parser {
 public:
  explicit MarkdownParser(const Dialect& dialect);

  void parse(const TextSource& text) override;
  void apply_edit(const TextRegion& region, const TextSource& text) override;
  const SyntaxTree& tree() const override { return tree_; }
  const Dialect& dialect() const override { return dialect_; }
  std::span<const TextRegion> changes() const override { return changes_; }
  void wipe() override;

  const LineTable& lines() const noexcept { return lines_; }
  const StateArena& arena() const noexcept { return *arena_; }
  const RefIndex& refs() const noexcept { return refs_; }
  const std::vector<std::string>& last_flipped() const noexcept { return last_flipped_; }

  struct RunStats {
    bool incremental = false;
    std::size_t restart_line = 0;
    std::size_t stop_line = 0;
    std::size_t lines_parsed = 0;
    std::size_t takes = 0;
  };
  const RunStats& last_run_stats() const noexcept { return stats_; }

  std::size_t fallback_count() const noexcept { return fallback_count_; }
  const std::string& last_fallback_reason() const noexcept { return last_fallback_reason_; }

  using Parser::apply_edit;
  using Parser::parse;

 private:
  void full_parse(const TextSource& text);
  void try_apply_edit(const TextRegion& region, const TextSource& text);

  Dialect dialect_;
  Features features_;
  std::unique_ptr<StateArena> arena_;
  LineTable lines_;
  SyntaxTree tree_;
  RefIndex refs_;
  std::vector<std::string> last_flipped_;
  std::vector<TextRegion> changes_;
  std::size_t arena_size_at_last_compact_ = 0;
  RunStats stats_;
  std::size_t fallback_count_ = 0;
  std::string last_fallback_reason_;
};

}  
