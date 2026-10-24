#pragma once

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QPalette>
#include <QTextCharFormat>

#include <array>
#include <cstddef>

#include "lumos/syntax.hpp"

namespace lumos::qt {

struct StyleTable {
  std::array<QTextCharFormat, node_kind_count> kinds;
  std::array<QTextCharFormat, 6> headings;
  QColor code_line_background;
  QTextCharFormat find_hit;
  QTextCharFormat find_hit_current;
  QBrush page_background;
  QBrush outside_background;
  int page_padding = 24;
  int page_margin = 12;
  QBrush gutter_background;
  QColor gutter_text;

  QTextCharFormat format(const Node& node) const;
  bool has_line_background(const Node& node) const;

  QTextCharFormat& operator[](NodeKind kind) { return kinds[static_cast<std::size_t>(kind)]; }
  const QTextCharFormat& operator[](NodeKind kind) const { return kinds[static_cast<std::size_t>(kind)]; }

  static StyleTable make_default(const QFont& base, const QPalette& palette);
};

}  
