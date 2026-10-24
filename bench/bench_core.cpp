
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "lumos/document.hpp"
#include "lumos/log.hpp"
#include "lumos/parser.hpp"

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::string out(static_cast<std::size_t>(std::filesystem::file_size(path)), '\0');
  in.read(out.data(), static_cast<std::streamsize>(out.size()));
  return out;
}

std::string make_text(const std::string& sample, std::size_t lines) {
  const auto per = static_cast<std::size_t>(std::count(sample.begin(), sample.end(), '\n'));
  std::string text;
  for (std::size_t n = 0; n < lines; n += per) text += sample;
  return text;
}

std::string make_lines(std::string_view line, std::size_t lines) {
  std::string text;
  for (std::size_t n = 0; n < lines; ++n) text += line;
  return text;
}

void replace_all(std::string& s, std::string_view from, std::string_view to) {
  for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
    s.replace(at, from.size(), to);
}

struct Stats {
  std::vector<double> ms;
  void print(const char* name) {
    if (ms.empty()) return;
    std::sort(ms.begin(), ms.end());
    double sum = 0;
    for (double v : ms) sum += v;
    std::printf("%-12s n=%zu mean_ms=%.3f median_ms=%.3f max_ms=%.3f\n", name, ms.size(),
                sum / static_cast<double>(ms.size()), ms[ms.size() / 2], ms.back());
  }
};

}  

int main(int argc, char** argv) {
  std::size_t lines = 5000;
  int keys = 50;
  std::string which = "paragraph";
  std::string kind = "sample";
  bool show_log = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "--lines" && i + 1 < argc) {
      lines = std::strtoul(argv[++i], nullptr, 10);
    } else if (a == "--keys" && i + 1 < argc) {
      keys = std::atoi(argv[++i]);
    } else if (a == "--doc" && i + 1 < argc) {
      kind = argv[++i];
    } else if (a == "--case" && i + 1 < argc) {
      which = argv[++i];
    } else if (a == "--log") {
      show_log = true;
    } else {
      std::fprintf(stderr, "usage: lumos_bench [--lines N] [--keys K] [--doc KIND] [--case NAME] [--log]\n");
      return 2;
    }
  }
  const std::string sample = read_file(std::filesystem::path(LUMOS_TEST_DATA_DIR) / "sample.md");
  std::string text;
  if (kind == "list") {
    text = make_lines("- Intro with item text and *em*\n", lines);
  } else if (kind == "quote") {
    text = make_lines("> Intro with quoted text and *em*\n", lines);
  } else {
    text = make_text(sample, lines);
  }
  if (which == "open_def") {
    const std::size_t at = text.find("# Sample");
    if (at != std::string::npos) text.insert(at, "[od]: /od\n<b>\n\n");
  }
  if (which == "def_rename") {
    replace_all(text, "[ref][1]", "[ref][zz]");
    text += "\n[zz]: /zz\n";
  }

  if (which == "lumos_parse") {
    Stats parse;
    std::size_t nodes = 0;
    for (int i = 0; i < std::max(keys, 1); ++i) {
      auto p = lumos::md::make_markdown_parser();
      const auto s = Clock::now();
      p->parse(text);
      parse.ms.push_back(ms_since(s));
      nodes = p->tree().size();
    }
    std::printf("doc=%s lines=%zu bytes=%zu nodes=%zu parser=lumos\n", kind.c_str(), lines, text.size(), nodes);
    parse.print("lumos_parse");
    return 0;
  }

  if (which == "lumos_edit") {
    auto p = lumos::md::make_markdown_parser();
    p->parse(text);
    std::size_t line_start = text.rfind('\n', text.size() / 2);
    line_start = line_start == std::string::npos ? 0 : line_start + 1;
    std::size_t line_end = text.find('\n', line_start);
    if (line_end == std::string::npos) line_end = text.size();
    const std::size_t at = line_start + (line_end - line_start) / 2;
    Stats edit_stats;
    for (int i = 0; i < std::max(keys, 1); ++i) {
      text.insert(at, "x");
      auto s = Clock::now();
      p->apply_edit(lumos::Edit{at, 0, "x"}, text);
      edit_stats.ms.push_back(ms_since(s));
      text.erase(at, 1);
      s = Clock::now();
      p->apply_edit(lumos::Edit{at, 1, ""}, text);
      edit_stats.ms.push_back(ms_since(s));
    }
    std::printf("doc=%s lines=%zu bytes=%zu parser=lumos\n", kind.c_str(), lines, text.size());
    edit_stats.print("lumos_edit");
    return 0;
  }

  auto t = Clock::now();
  lumos::Document doc(text);
  const double load_ms = ms_since(t);
  std::printf("doc=%s lines=%zu bytes=%zu nodes=%zu load_ms=%.1f\n", kind.c_str(), lines, text.size(),
              doc.tree().size(), load_ms);

  if (show_log) {
    if (std::getenv("LUMOS_BENCH_DEBUG")) lumos::set_log_level(lumos::LogLevel::debug);
    lumos::set_log_sink([](lumos::LogLevel, std::string_view l) {
      if (l.find("comp=parser") != std::string_view::npos || l.find("comp=document") != std::string_view::npos)
        std::fprintf(stderr, "%.*s\n", static_cast<int>(l.size()), l.data());
    });
  }

  const std::size_t mid = text.find("Intro with", text.size() / 2);
  const std::size_t top = text.find("# Sample");
  Stats edit;
  auto apply = [&](const lumos::Edit& e) {
    const auto s = Clock::now();
    doc.apply(e);
    edit.ms.push_back(ms_since(s));
  };

  if (which == "paragraph" || which == "open_def") {
    const std::size_t at = mid + 5;
    for (int i = 0; i < keys; ++i) apply({at + static_cast<std::size_t>(i), 0, "x"});
  } else if (which == "fence_close") {
    for (int i = 0; i < keys; ++i) {
      apply({top, 0, "```\n"});
      apply({top, 4, ""});
    }
  } else if (which == "fence_open") {
    const std::size_t at = top == std::string::npos ? 0 : top;
    for (int i = 0; i < keys; ++i) {
      apply({at, 0, "````\n"});
      apply({at, 5, ""});
    }
  } else if (which == "def_rename") {
    const std::size_t at = text.size() - 5;  
    for (int i = 0; i < keys; ++i) {
      apply({at, 0, "q"});
      apply({at, 1, ""});
    }
  } else if (which == "heading_end") {
    const std::string line = "\n## H";
    for (int i = 0; i < keys; ++i) {
      for (std::size_t k = 0; k < line.size(); ++k) apply({doc.size(), 0, line.substr(k, 1)});
      apply({doc.size() - line.size(), line.size(), ""});
    }
  } else if (which == "def") {
    const std::size_t at = text.rfind("\n\n", mid) + 2;
    const std::string def = "[zz]: /u\n\n";
    for (int i = 0; i < keys; ++i) {
      apply({at, 0, def});
      apply({at, def.size(), ""});
    }
  } else if (which == "setext") {
    const std::size_t at = text.find('\n', mid) + 1;
    for (int i = 0; i < keys; ++i) {
      apply({at, 0, "===\n"});
      apply({at, 4, ""});
    }
  } else {
    std::fprintf(stderr, "unknown case: %s\n", which.c_str());
    return 2;
  }
  lumos::set_log_sink(nullptr);
  edit.print(which.c_str());
  return 0;
}
