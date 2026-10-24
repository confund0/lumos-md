#include "lumos/qt/style_table.hpp"

#include <QFontDatabase>

namespace lumos::qt {
namespace {

QColor mix(const QColor& a, const QColor& b, double t) {
  auto ch = [t](int x, int y) { return static_cast<int>(x + (y - x) * t + 0.5); };
  return QColor(ch(a.red(), b.red()), ch(a.green(), b.green()), ch(a.blue(), b.blue()));
}

}  

QTextCharFormat StyleTable::format(const Node& node) const {
  QTextCharFormat f = (*this)[node.kind];
  if (node.kind == NodeKind::heading && node.level >= 1 && node.level <= 6)
    f.merge(headings[static_cast<std::size_t>(node.level - 1)]);
  return f;
}

bool StyleTable::has_line_background(const Node& node) const {
  return node.kind == NodeKind::code_block || node.kind == NodeKind::front_matter;
}

StyleTable StyleTable::make_default(const QFont& base, const QPalette& palette) {
  StyleTable t;
  const QColor text = palette.color(QPalette::Active, QPalette::Text);
  const QColor back = palette.color(QPalette::Active, QPalette::Base);
  auto hue = [&text](int r, int g, int b) { return mix(QColor(r, g, b), text, 0.3); };
  const QColor marker = mix(text, back, 0.45);
  const QColor link = palette.color(QPalette::Active, QPalette::Link);
  const QColor link_part = mix(link, back, 0.3);
  const QColor html = hue(0xc0, 0x60, 0x30);
  const QColor code_bg = mix(back, text, 0.08);
  const QStringList mono{QFontDatabase::systemFont(QFontDatabase::FixedFont).family()};

  auto color = [](const QColor& c) {
    QTextCharFormat f;
    f.setForeground(c);
    return f;
  };

  t[NodeKind::heading].setFontWeight(QFont::Bold);
  const double sizes[6] = {2.0, 1.6, 1.35, 1.2, 1.1, 1.0};
  const double point = base.pointSizeF() > 0 ? base.pointSizeF() : 10.0;
  for (std::size_t i = 0; i < 6; ++i) t.headings[i].setFontPointSize(point * sizes[i]);
  t[NodeKind::block_quote] = color(hue(0x5a, 0x8a, 0x5a));
  t[NodeKind::block_quote].setFontItalic(true);
  t[NodeKind::code_block].setFontFamilies(mono);
  t[NodeKind::code_content].setFontFamilies(mono);
  t[NodeKind::html_block] = color(html);
  t[NodeKind::thematic_break] = color(marker);
  t[NodeKind::table_header_row].setFontWeight(QFont::Bold);
  t[NodeKind::table_delimiter_row] = color(marker);
  t[NodeKind::front_matter] = color(mix(text, back, 0.3));
  t[NodeKind::front_matter].setFontFamilies(mono);

  t[NodeKind::emphasis].setFontItalic(true);
  t[NodeKind::strong].setFontWeight(QFont::Bold);
  t[NodeKind::strikethrough].setFontStrikeOut(true);
  t[NodeKind::code_span].setFontFamilies(mono);
  t[NodeKind::code_span].setBackground(code_bg);
  t[NodeKind::link] = color(link);
  t[NodeKind::image] = color(link);
  t[NodeKind::autolink] = color(link);
  t[NodeKind::autolink].setFontUnderline(true);
  t[NodeKind::html_inline] = color(html);
  t[NodeKind::entity] = color(hue(0xb0, 0x70, 0x10));
  t[NodeKind::math] = color(hue(0x90, 0x50, 0xb0));

  t[NodeKind::link_text].setFontUnderline(true);
  t[NodeKind::link_destination] = color(link_part);
  t[NodeKind::link_title] = color(hue(0x60, 0x90, 0x60));
  t[NodeKind::link_label] = color(link_part);
  t[NodeKind::info_string] = color(html);
  for (NodeKind k : {NodeKind::emphasis_delimiter, NodeKind::heading_marker, NodeKind::code_fence_delimiter,
                     NodeKind::code_span_delimiter, NodeKind::link_delimiter, NodeKind::block_quote_marker,
                     NodeKind::escape, NodeKind::math_delimiter})
    t[k] = color(marker);
  t[NodeKind::list_marker] = color(hue(0x60, 0x80, 0xb0));
  t[NodeKind::list_marker].setFontWeight(QFont::Bold);
  t[NodeKind::task_marker] = color(hue(0x60, 0x80, 0xb0));
  t[NodeKind::table_pipe] = color(mix(hue(0x60, 0x80, 0xb0), back, 0.3));

  t.code_line_background = mix(back, text, 0.05);
  t.page_background = back;
  t.outside_background = back;
  t.gutter_background = mix(back, text, 0.04);
  t.gutter_text = mix(text, back, 0.55);

  const QColor highlight = palette.color(QPalette::Active, QPalette::Highlight);
  t.find_hit.setBackground(mix(highlight, back, 0.6));
  t.find_hit_current.setBackground(mix(highlight, back, 0.25));
  return t;
}

}  
