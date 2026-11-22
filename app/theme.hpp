#pragma once

#include <QColor>
#include <QPalette>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

enum class ThemeRole : std::uint8_t {
  bg_base,       
  bg_bar,        
  bg_field,      
  bg_hover,      
  divider,       
  sel_bg,        
  sel_fg,        
  fg_primary,    
  fg_secondary,  
  fg_dim,        
  fg_link,       
  bg_banner,     
};

inline constexpr std::size_t kThemeRoleCount = 12;

inline constexpr std::array<std::string_view, kThemeRoleCount> kThemeRoleNames = {
    "bg_base", "bg_bar",    "bg_field", "bg_hover", "divider", "sel_bg",
    "sel_fg",  "fg_primary", "fg_secondary", "fg_dim", "fg_link", "bg_banner",
};

struct Theme {
  std::string_view id;
  std::string_view name;
  std::optional<std::array<std::uint32_t, kThemeRoleCount>> values;
};

std::span<const Theme> themes();
const Theme& find_theme(QStringView id);

QColor theme_color(const Theme& theme, ThemeRole role, const QPalette& system);

QPalette theme_palette(const Theme& theme);

QString fill_stylesheet(QStringView qss, const Theme& theme, const QPalette& system, QStringList* unknown = nullptr);

QString theme_stylesheet(const Theme& theme, const QPalette& system);

void apply_theme(const Theme& theme);
