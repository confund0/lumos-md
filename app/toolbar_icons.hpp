#pragma once

#include <QColor>
#include <QIcon>

enum class ToolIcon {
  new_file,
  open,
  save,
  undo,
  redo,
  find,
  bold,
  italic,
  strike,
  code,
  link,
  bullet_list,
  ordered_list,
  task_list,
  quote,
  renumber,
  horizontal_line,
  code_block,
  table,
  format_table,
  paragraph,
  caret,
  words,
  document,
  zoom,
  mcp,
};

QIcon toolbar_icon(ToolIcon kind);
QIcon toolbar_icon(ToolIcon kind, const QColor& color);
