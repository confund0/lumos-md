#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "lumos/document.hpp"
#include "lumos/types.hpp"

namespace lumos {

struct Selection {
  std::size_t anchor = 0;
  std::size_t cursor = 0;

  bool operator==(const Selection&) const = default;
};

struct CommandResult {
  std::vector<Edit> edits;
  Selection selection;  
};

std::optional<CommandResult> insert_newline(const Document& doc, Selection sel, std::string_view newline);

std::optional<CommandResult> insert_line_break(const Document& doc, Selection sel, std::string_view newline);

std::optional<CommandResult> indent_list(const Document& doc, Selection sel);

std::optional<CommandResult> outdent_list(const Document& doc, Selection sel);

std::optional<CommandResult> delete_markup_backward(const Document& doc, Selection sel);

enum class InlineKind : std::uint8_t { strong, emphasis, strikethrough, code };

std::optional<CommandResult> next_cell(const Document& doc, Selection sel, std::string_view newline);

std::optional<CommandResult> previous_cell(const Document& doc, Selection sel);

std::optional<CommandResult> next_row(const Document& doc, Selection sel, std::string_view newline);

std::optional<CommandResult> previous_row(const Document& doc, Selection sel);

std::optional<CommandResult> format_table(const Document& doc, Selection sel);

std::optional<CommandResult> insert_table(const Document& doc, Selection sel, int rows, int columns,
                                          std::string_view newline);

std::optional<CommandResult> toggle_inline(const Document& doc, Selection sel, InlineKind kind);

std::optional<CommandResult> insert_link(const Document& doc, Selection sel, std::string_view text,
                                         std::string_view url);

std::optional<CommandResult> insert_image(const Document& doc, Selection sel, std::string_view alt,
                                          std::string_view url);

std::optional<CommandResult> remove_link(const Document& doc, Selection sel);

std::optional<CommandResult> set_heading(const Document& doc, Selection sel, int level);

enum class BlockKind : std::uint8_t { bullet_list, ordered_list, task_list, block_quote, code_block };

std::optional<CommandResult> toggle_block(const Document& doc, Selection sel, BlockKind kind);

std::optional<CommandResult> insert_thematic_break(const Document& doc, Selection sel);

struct ListShape {
  struct Item {
    int depth = 0;  
    bool ordered = false;
    bool operator==(const Item&) const = default;
  };
  std::vector<Item> items;  
  std::vector<std::pair<int, std::uint64_t>> first_numbers;
};

ListShape list_shape(const Document& doc, std::size_t from, std::size_t to);

std::optional<CommandResult> renumber_lists(const Document& doc, const ListShape& before, std::size_t from,
                                            std::size_t to, Selection sel);

std::vector<ByteRange> out_of_sequence(const Document& doc);

std::optional<CommandResult> renumber(const Document& doc, Selection sel);

std::optional<CommandResult> marker_to_text(const Document& doc, Selection sel);

enum class ListKind : std::uint8_t { none, bullet, ordered };

struct Format {
  bool strong = false;
  bool emphasis = false;
  bool strikethrough = false;
  bool code = false;
  bool link = false;
  int heading_level = 0;  
  ListKind list = ListKind::none;
  bool task = false;
  bool block_quote = false;
  bool table = false;
  bool code_block = false;  

  bool operator==(const Format&) const = default;
};

Format format_at(const Document& doc, Selection sel);

std::optional<CommandResult> drop_text(const Document& doc, ByteRange source, std::size_t target, bool move);

struct FindOptions {
  bool case_sensitive = false;
  bool whole_word = false;
  bool regex = false;

  bool operator==(const FindOptions&) const = default;
};

std::optional<std::vector<ByteRange>> find_all(const Document& doc, std::string_view pattern,
                                               FindOptions options);

std::optional<CommandResult> replace_one(const Document& doc, ByteRange match, std::string_view pattern,
                                         std::string_view replacement, FindOptions options);

std::optional<CommandResult> replace_all(const Document& doc, std::string_view pattern,
                                         std::string_view replacement, FindOptions options);

}  
