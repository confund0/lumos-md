
#include "theme.hpp"

#include <QApplication>
#include <QRegularExpression>

#include <string>

#include "lumos/log.hpp"

namespace {

constexpr std::string_view kComp = "app";

constexpr auto kThemes = std::to_array<Theme>({
    {"default", "Default", std::nullopt},
    {"light_grey",
     "Light grey",
     std::array<std::uint32_t, kThemeRoleCount>{
         0xD4D4D4,  
         0xC4C4C4,  
         0xD9D9D9,  
         0xD0D0D0,  
         0x9A9A9A,  
         0x4A4A4A,  
         0xFFFFFF,  
         0x1C1C1C,  
         0x333333,  
         0x4A4A4A,  
         0x1F4F8A,  
         0xDCDCDC,  
     }},
    {"gruvbox",
     "Gruvbox",
     std::array<std::uint32_t, kThemeRoleCount>{
         0x1D2021,  
         0x282828,  
         0x32302F,  
         0x302E2C,  
         0x504945,  
         0x504945,  
         0xEBDBB2,  
         0xEBDBB2,  
         0xBDAE93,  
         0xA89984,  
         0x83A598,  
         0x32302F,  
     }},
    {"dark",
     "Dark",
     std::array<std::uint32_t, kThemeRoleCount>{
         0x242424,  
         0x2E2E2E,  
         0x2A2A2A,  
         0x353535,  
         0x444444,  
         0x2F5A8A,  
         0xFFFFFF,  
         0xDADADA,  
         0xB0B0B0,  
         0x8A8A8A,  
         0x78AEEB,  
         0x2E2E2E,  
     }},
    {"dark_blue_grey",
     "Dark blue-grey",
     std::array<std::uint32_t, kThemeRoleCount>{
         0x31363B,  
         0x2A2E32,  
         0x232629,  
         0x3B4045,  
         0x4D5359,  
         0x2C6A91,  
         0xFCFCFC,  
         0xEFF0F1,  
         0xBDC3C7,  
         0x7F8C8D,  
         0x3DAEE9,  
         0x31363B,  
     }},
    {"dark_slate",
     "Dark slate",
     std::array<std::uint32_t, kThemeRoleCount>{
         0x21252B,  
         0x21252B,  
         0x282C34,  
         0x2C313A,  
         0x3E4451,  
         0x3E4451,  
         0xD7DAE0,  
         0xABB2BF,  
         0x9DA5B4,  
         0x5C6370,  
         0x61AFEF,  
         0x2C313A,  
     }},
});

constexpr char16_t kStylesheet[] = uR"(
QTabWidget::pane { border: none; }
QStatusBar::item { border: none; }
QTabBar::tab {
  background: {{bg_bar}};
  color: {{fg_secondary}};
  border: 1px solid {{divider}};
  border-bottom: none;
  border-top-left-radius: 6px;
  border-top-right-radius: 6px;
  padding: 4px 10px;
  margin-top: 3px;
  margin-right: 2px;
}
QTabBar::tab:hover:!selected { background: {{bg_hover}}; }
QTabBar::tab:selected {
  background: {{bg_field}};
  color: {{fg_primary}};
  margin-top: 0;
}
QToolButton#tab_close {
  border: none;
  border-radius: 3px;
  padding: 0 3px;
  background: transparent;
  color: {{fg_secondary}};
}
QToolButton#tab_close:hover { background: {{divider}}; color: {{fg_primary}}; }
)";

QPalette::ColorRole system_role(ThemeRole role) {
  switch (role) {
    case ThemeRole::bg_base: return QPalette::Window;
    case ThemeRole::bg_bar: return QPalette::Button;
    case ThemeRole::bg_field: return QPalette::Base;
    case ThemeRole::bg_hover: return QPalette::Midlight;
    case ThemeRole::divider: return QPalette::Mid;
    case ThemeRole::sel_bg: return QPalette::Highlight;
    case ThemeRole::sel_fg: return QPalette::HighlightedText;
    case ThemeRole::fg_primary: return QPalette::WindowText;
    case ThemeRole::fg_secondary: return QPalette::ButtonText;
    case ThemeRole::fg_dim: return QPalette::PlaceholderText;
    case ThemeRole::fg_link: return QPalette::Link;
    case ThemeRole::bg_banner: return QPalette::ToolTipBase;
  }
  return QPalette::Window;
}

}  

std::span<const Theme> themes() { return kThemes; }

const Theme& find_theme(QStringView id) {
  for (const Theme& theme : kThemes)
    if (id == QLatin1StringView(theme.id.data(), static_cast<qsizetype>(theme.id.size()))) return theme;
  return kThemes.front();
}

QColor theme_color(const Theme& theme, ThemeRole role, const QPalette& system) {
  if (!theme.values) return system.color(QPalette::Active, system_role(role));
  return QColor::fromRgb(theme.values->at(static_cast<std::size_t>(role)));
}

QPalette theme_palette(const Theme& theme) {
  if (!theme.values) return QPalette();
  QPalette palette;
  const auto set = [&](QPalette::ColorRole qt_role, ThemeRole role) {
    palette.setColor(qt_role, theme_color(theme, role, palette));
  };
  set(QPalette::Window, ThemeRole::bg_base);
  set(QPalette::WindowText, ThemeRole::fg_primary);
  set(QPalette::Base, ThemeRole::bg_field);
  set(QPalette::AlternateBase, ThemeRole::bg_bar);
  set(QPalette::Text, ThemeRole::fg_primary);
  set(QPalette::PlaceholderText, ThemeRole::fg_dim);
  set(QPalette::Button, ThemeRole::bg_bar);
  set(QPalette::ButtonText, ThemeRole::fg_primary);
  set(QPalette::BrightText, ThemeRole::fg_primary);
  set(QPalette::Highlight, ThemeRole::sel_bg);
  set(QPalette::HighlightedText, ThemeRole::sel_fg);
  set(QPalette::Link, ThemeRole::fg_link);
  set(QPalette::LinkVisited, ThemeRole::fg_link);
  set(QPalette::ToolTipBase, ThemeRole::bg_banner);
  set(QPalette::ToolTipText, ThemeRole::fg_primary);
  set(QPalette::Light, ThemeRole::bg_field);
  set(QPalette::Midlight, ThemeRole::bg_banner);
  set(QPalette::Mid, ThemeRole::divider);
  set(QPalette::Dark, ThemeRole::divider);
  set(QPalette::Shadow, ThemeRole::divider);
  const QColor dim = theme_color(theme, ThemeRole::fg_dim, palette);
  const QColor bar = theme_color(theme, ThemeRole::bg_bar, palette);
  const QColor disabled((dim.red() + bar.red()) / 2, (dim.green() + bar.green()) / 2, (dim.blue() + bar.blue()) / 2);
  for (const QPalette::ColorRole qt_role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
    palette.setColor(QPalette::Disabled, qt_role, disabled);
  return palette;
}

QString fill_stylesheet(QStringView qss, const Theme& theme, const QPalette& system, QStringList* unknown) {
  static const QRegularExpression kPlaceholder(QStringLiteral(R"(\{\{([a-z0-9_]+)\}\})"));
  QString out;
  out.reserve(qss.size());
  qsizetype last = 0;
  QRegularExpressionMatchIterator it = kPlaceholder.globalMatchView(qss);
  while (it.hasNext()) {
    const QRegularExpressionMatch match = it.next();
    out += qss.mid(last, match.capturedStart() - last);
    last = match.capturedEnd();
    const QStringView name = match.capturedView(1);
    bool found = false;
    for (std::size_t i = 0; i < kThemeRoleCount && !found; ++i) {
      if (name == QLatin1StringView(kThemeRoleNames[i].data(), static_cast<qsizetype>(kThemeRoleNames[i].size()))) {
        out += theme_color(theme, static_cast<ThemeRole>(i), system).name(QColor::HexRgb);
        found = true;
      }
    }
    if (!found) {
      out += match.capturedView(0);
      if (unknown != nullptr) unknown->append(name.toString());
    }
  }
  out += qss.mid(last);
  return out;
}

QString theme_stylesheet(const Theme& theme, const QPalette& system) {
  return fill_stylesheet(kStylesheet, theme, system);
}

void apply_theme(const Theme& theme) {
  QApplication::setPalette(theme_palette(theme));
  qApp->setStyleSheet(theme_stylesheet(theme, QApplication::palette()));
  lumos::log(lumos::LogLevel::debug, kComp, "theme", {{"id", std::string(theme.id)}});
}
