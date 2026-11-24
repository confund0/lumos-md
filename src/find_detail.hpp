#pragma once

#include <chrono>
#include <string_view>

#include "lumos/commands.hpp"
#include "lumos/document.hpp"

namespace lumos::find_detail {

FindResult find_all_within(const Document& doc, std::string_view pattern, FindOptions options,
                           std::chrono::steady_clock::duration budget);

ReplaceAllResult replace_all_within(const Document& doc, std::string_view pattern, std::string_view replacement,
                                    FindOptions options, std::chrono::steady_clock::duration budget);

}  
