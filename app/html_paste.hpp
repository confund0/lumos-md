#pragma once

#include <optional>
#include <string_view>

#include <QString>

#include "lumos/commands.hpp"
#include "lumos/document.hpp"

std::optional<lumos::CommandResult> paste_html(const lumos::Document& doc, lumos::Selection sel,
                                               const QString& html, std::string_view newline);
