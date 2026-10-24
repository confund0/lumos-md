
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "lumos/md/html_renderer.hpp"
#include "lumos/parser.hpp"

namespace {

struct Example {
  int number = 0;
  std::string section;
  std::string markdown;
  std::string html;
  std::string tags;  
};

struct SpecFile {
  std::string name;
  lumos::Dialect dialect;
  bool tagfilter = false;
  bool (*selected)(const Example&) = nullptr;
};

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot read %s\n", path.c_str());
    std::exit(2);
  }
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void replace_all(std::string& s, std::string_view from, std::string_view to) {
  for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
    s.replace(at, from.size(), to);
}

std::vector<Example> parse_spec(const std::string& text) {
  static const std::string fence(32, '`');
  std::vector<Example> out;
  std::istringstream in(text);
  std::string line;
  std::string section;
  int state = 0;  
  Example ex;
  int number = 0;
  while (std::getline(in, line)) {
    if (state == 0) {
      if (line.rfind(fence + " example", 0) == 0) {
        state = 1;
        ex = Example{};
        ex.number = ++number;
        ex.section = section;
        ex.tags = line.size() > fence.size() + 8 ? line.substr(fence.size() + 9) : "";
      } else if (!line.empty() && line[0] == '#') {
        const std::size_t sp = line.find(' ');
        if (sp != std::string::npos && line.find_first_not_of('#') == sp) section = line.substr(sp + 1);
      }
    } else if (state == 1) {
      if (line == ".") {
        state = 2;
      } else {
        ex.markdown += line + "\n";
      }
    } else {
      if (line == fence) {
        state = 0;
        replace_all(ex.markdown, "\xE2\x86\x92", "\t");
        replace_all(ex.html, "\xE2\x86\x92", "\t");
        out.push_back(ex);
      } else {
        ex.html += line + "\n";
      }
    }
  }
  return out;
}

std::map<std::string, std::set<int>> read_expected(const std::string& path) {
  std::map<std::string, std::set<int>> out;
  std::istringstream in(read_file(path));
  std::string line;
  std::string file;
  while (std::getline(in, line)) {
    line = line.substr(0, line.find('#'));
    if (!line.empty() && line[0] == '[') {
      file = line.substr(1, line.find(']') - 1);
      out[file];
      continue;
    }
    std::istringstream words(line);
    std::string w;
    while (words >> w) {
      const std::size_t dash = w.find('-');
      const int a = std::atoi(w.substr(0, dash).c_str());
      const int b = dash == std::string::npos ? a : std::atoi(w.substr(dash + 1).c_str());
      for (int i = a; i <= b; ++i) out[file].insert(i);
    }
  }
  return out;
}

std::string format_ranges(const std::vector<int>& v) {
  std::string out;
  std::size_t col = 0;
  for (std::size_t i = 0; i < v.size();) {
    std::size_t j = i;
    while (j + 1 < v.size() && v[j + 1] == v[j] + 1) ++j;
    std::string w = std::to_string(v[i]);
    if (j > i) w += "-" + std::to_string(v[j]);
    if (col + w.size() + 1 > 72) {
      out += "\n";
      col = 0;
    } else if (col > 0) {
      out += " ";
      ++col;
    }
    out += w;
    col += w.size();
    i = j + 1;
  }
  return out;
}

std::string show(std::string s) {
  replace_all(s, "\t", "\xE2\x86\x92");
  return s;
}

struct Result {
  bool pass = false;
  std::string actual;
  std::string tree;
  std::string invalid;  
};

Result run_example(const SpecFile& f, const Example& ex) {
  Result r;
  auto parser = lumos::md::make_markdown_parser(f.dialect);
  const lumos::StringSource src(ex.markdown);
  parser->parse(src);
  const lumos::SyntaxTree& tree = parser->tree();
  lumos::md::HtmlOptions opt;
  opt.unsafe = true;
  opt.tagfilter = f.tagfilter;
  r.actual = lumos::md::render_html(tree, src, opt);
  r.tree = tree.to_debug_string();
  if (auto e = tree.validate(ex.markdown.size())) r.invalid = "validate: " + *e;
  if (auto e = tree.validate_coverage(ex.markdown)) r.invalid = "validate_coverage: " + *e;
  r.pass = r.actual == ex.html;
  return r;
}

void print_details(const SpecFile& f, const Example& ex, const Result& r) {
  std::printf("--- %s example %d (%s)%s\n", f.name.c_str(), ex.number, ex.section.c_str(),
              r.pass ? " passes" : " fails");
  std::printf("input:\n%s", show(ex.markdown).c_str());
  std::printf("expected:\n%s", show(ex.html).c_str());
  std::printf("actual:\n%s", show(r.actual).c_str());
  std::printf("tree: %s\n", r.tree.c_str());
  if (!r.invalid.empty()) std::printf("invalid tree: %s\n", r.invalid.c_str());
}

}  

int main(int argc, char** argv) {
  bool sections = false;
  bool list_failures = false;
  bool verbose = false;
  std::string only_file;
  int only_example = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "--sections") {
      sections = true;
    } else if (a == "--list-failures") {
      list_failures = true;
    } else if (a == "--verbose") {
      verbose = true;
    } else if (a == "--file" && i + 1 < argc) {
      only_file = argv[++i];
    } else if (a == "--example" && i + 1 < argc) {
      only_example = std::atoi(argv[++i]);
    } else {
      std::fprintf(stderr,
                   "usage: lumos_spec [--sections] [--file NAME] [--example N] [--list-failures] [--verbose]\n");
      return 2;
    }
  }
  const std::string dir = LUMOS_SPEC_DIR;
  lumos::Dialect gfm;
  gfm.front_matter = false;
  const std::vector<SpecFile> files = {
      {"commonmark-0.31.2.txt", lumos::Dialect::commonmark(), false, [](const Example&) { return true; }},
      {"gfm-0.29.txt", gfm, true,
       [](const Example& e) { return !e.tags.empty() && e.tags.find("disabled") == std::string::npos; }},
      {"extensions.txt", gfm, true,
       [](const Example& e) { return e.section.find("ootnote") == std::string::npos && e.html != "<IGNORE>\n"; }},
  };
  auto expected = read_expected(dir + "/expected_failures.txt");

  if (only_example > 0) {
    const std::string name = only_file.empty() ? files[0].name : only_file;
    for (const SpecFile& f : files) {
      if (f.name != name) continue;
      for (const Example& ex : parse_spec(read_file(dir + "/" + f.name)))
        if (ex.number == only_example) print_details(f, ex, run_example(f, ex));
    }
    return 0;
  }

  int unexpected = 0;
  int shown = 0;
  int total_pass = 0;
  int total_run = 0;
  for (const SpecFile& f : files) {
    if (!only_file.empty() && f.name != only_file) continue;
    const std::set<int>& exp = expected[f.name];
    std::vector<std::pair<std::string, std::pair<int, int>>> per_section;  
    std::vector<int> failing;
    int pass = 0;
    int run = 0;
    for (const Example& ex : parse_spec(read_file(dir + "/" + f.name))) {
      if (!f.selected(ex)) continue;
      const Result r = run_example(f, ex);
      ++run;
      if (per_section.empty() || per_section.back().first != ex.section) per_section.push_back({ex.section, {0, 0}});
      ++per_section.back().second.second;
      if (r.pass) {
        ++pass;
        ++per_section.back().second.first;
      } else {
        failing.push_back(ex.number);
      }
      const bool listed = exp.count(ex.number) > 0;
      const bool bad = !r.invalid.empty() || r.pass == listed;
      if (bad) {
        ++unexpected;
        if (!list_failures && (verbose || shown < 5)) {
          ++shown;
          if (!r.invalid.empty()) {
            std::printf("INVALID TREE: ");
          } else if (r.pass) {
            std::printf("NOW PASSES (remove it from expected_failures.txt): ");
          } else {
            std::printf("UNEXPECTED FAILURE: ");
          }
          print_details(f, ex, r);
        }
      }
    }
    std::printf("%s: %d of %d examples pass\n", f.name.c_str(), pass, run);
    total_pass += pass;
    total_run += run;
    if (sections)
      for (const auto& [name, counts] : per_section)
        std::printf("  %-45s %3d / %3d\n", name.c_str(), counts.first, counts.second);
    if (list_failures) std::printf("[%s]\n%s\n", f.name.c_str(), format_ranges(failing).c_str());
  }
  std::printf("total: %d of %d examples pass\n", total_pass, total_run);
  if (unexpected > 0) {
    std::printf("%d unexpected results\n", unexpected);
    return 1;
  }
  return 0;
}
