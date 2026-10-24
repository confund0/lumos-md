
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "lumos/parser.hpp"
#include "lumos/syntax.hpp"
#include "lumos/types.hpp"
#include "md/md_parser.hpp"
#include "md_test_util.hpp"

namespace md = lumos::md;
using lumos::Edit;

namespace {

struct Failure {
  bool is_fallback = false;
  std::string detail;  
};

std::optional<Failure> check_edit(const std::string& before, const Edit& edit) {
  if (edit.offset > before.size() || edit.removed > before.size() - edit.offset) return std::nullopt;
  auto inc = md::make_markdown_parser();
  const auto* mp = dynamic_cast<const md::MarkdownParser*>(inc.get());
  inc->parse(before);
  const lumos::SyntaxTree before_tree = inc->tree();
  std::string after = before;
  after.replace(edit.offset, edit.removed, edit.inserted);
  try {
    inc->apply_edit(edit, after);
  } catch (const std::exception& e) {
    return Failure{true, std::string("crash: ") + e.what()};
  }
  if (mp->fallback_count() != 0) return Failure{true, mp->last_fallback_reason()};
  auto fresh = md::make_markdown_parser();
  const auto* fresh_mp = dynamic_cast<const md::MarkdownParser*>(fresh.get());
  fresh->parse(after);
  std::string diff = lumos_test::diff_trees(inc->tree(), fresh->tree());
  if (!diff.empty()) return Failure{false, diff};
  const std::string refs_diff = lumos_test::diff_refs(mp->refs(), fresh_mp->refs());
  if (!refs_diff.empty()) return Failure{false, "refs: " + refs_diff};
  if (const auto err = inc->tree().validate(after.size())) return Failure{false, "validate: " + *err};
  if (const auto err = inc->tree().validate_coverage(after)) return Failure{false, "validate_coverage: " + *err};
  const std::string locality = lumos_test::check_locality(before_tree, inc->tree(), inc->changes());
  if (!locality.empty()) return Failure{false, "locality: " + locality};
  return std::nullopt;
}

std::string lines_around(const std::string& text, std::size_t offset, int max_lines) {
  const std::size_t pos = std::min(offset, text.size());
  std::size_t start = pos;
  for (int back = max_lines / 2; back > 0 && start > 0; --back) {
    const std::size_t nl = text.rfind('\n', start - 1);
    if (nl == std::string::npos) {
      start = 0;
      break;
    }
    start = nl + 1;
  }
  std::size_t end = pos;
  for (int fwd = max_lines - max_lines / 2; fwd > 0 && end < text.size(); --fwd) {
    const std::size_t nl = text.find('\n', end);
    if (nl == std::string::npos) {
      end = text.size();
      break;
    }
    end = nl + 1;
  }
  return "lines [" + std::to_string(start) + ".." + std::to_string(end) + "):\n" + text.substr(start, end - start);
}

std::string cpp_escape(const std::string& s) {
  std::string out = "\"";
  for (char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\t') out += "\\t";
    else if (c == '\r') out += "\\r";
    else if (c < 0x20 || c == 0x7f) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\x%02x", c);
      out += buf;
    } else {
      out += ch;
    }
  }
  out += "\"";
  return out;
}

std::pair<std::string, Edit> minimize(std::string before, Edit edit, const Failure& original) {
  auto still_fails = [&](const std::string& t, const Edit& e) {
    const auto f = check_edit(t, e);
    if (!f.has_value() || f->is_fallback != original.is_fallback) return false;
    if (f->is_fallback) return f->detail == original.detail;
    return true;
  };
  bool changed = true;
  while (changed) {
    changed = false;
    for (;;) {
      if (before.empty()) break;
      std::size_t end = before.size();
      if (before.back() == '\n') --end;
      const std::size_t nl = end > 0 ? before.rfind('\n', end - 1) : std::string::npos;
      const std::size_t new_len = nl == std::string::npos ? 0 : nl + 1;
      if (new_len >= before.size() || new_len < edit.offset + edit.removed) break;
      const std::string candidate = before.substr(0, new_len);
      if (!still_fails(candidate, edit)) break;
      before = candidate;
      changed = true;
    }
    for (;;) {
      const std::size_t nl = before.find('\n');
      if (nl == std::string::npos) break;
      const std::size_t cut = nl + 1;
      if (cut > edit.offset) break;
      Edit candidate_edit = edit;
      candidate_edit.offset -= cut;
      const std::string candidate_text = before.substr(cut);
      if (!still_fails(candidate_text, candidate_edit)) break;
      before = candidate_text;
      edit = candidate_edit;
      changed = true;
    }
    for (;;) {
      if (before.size() <= edit.offset + edit.removed) break;
      const std::string candidate = before.substr(0, before.size() - 1);
      if (!still_fails(candidate, edit)) break;
      before = candidate;
      changed = true;
    }
    for (;;) {
      if (edit.offset == 0) break;
      Edit candidate_edit = edit;
      candidate_edit.offset -= 1;
      const std::string candidate_text = before.substr(1);
      if (!still_fails(candidate_text, candidate_edit)) break;
      before = candidate_text;
      edit = candidate_edit;
      changed = true;
    }
  }
  return {before, edit};
}

std::string signature(const std::string& before, const Edit& edit) {
  if (edit.offset > before.size() || edit.removed > before.size() - edit.offset) return "";
  auto inc = md::make_markdown_parser();
  const auto* mp = dynamic_cast<const md::MarkdownParser*>(inc.get());
  inc->parse(before);
  std::string after = before;
  after.replace(edit.offset, edit.removed, edit.inserted);
  try {
    inc->apply_edit(edit, after);
  } catch (const std::exception& e) {
    return std::string("throw: ") + e.what();
  }
  if (mp->fallback_count() != 0) return "fallback: " + mp->last_fallback_reason();
  auto fresh = md::make_markdown_parser();
  const auto* fresh_mp = dynamic_cast<const md::MarkdownParser*>(fresh.get());
  fresh->parse(after);
  std::string sig = lumos_test::tree_signature(inc->tree(), fresh->tree());
  if (sig.empty() && !lumos_test::diff_refs(mp->refs(), fresh_mp->refs()).empty()) sig = "refs_mismatch";
  return sig;
}

std::pair<std::string, Edit> shrink_keeping(std::string before, Edit edit, const std::string& sig) {
  auto same = [&](const std::string& t, const Edit& e) { return signature(t, e) == sig; };
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t ls = 0; ls < before.size();) {
      const std::size_t nl = before.find('\n', ls);
      const std::size_t le = nl == std::string::npos ? before.size() : nl + 1;
      const bool holds_edit = !(le <= edit.offset || ls >= edit.offset + edit.removed) ||
                              (edit.removed == 0 && edit.offset >= ls && edit.offset < le);
      if (!holds_edit) {
        const std::string t = before.substr(0, ls) + before.substr(le);
        Edit e = edit;
        if (le <= edit.offset) e.offset -= le - ls;
        if (same(t, e)) {
          before = t;
          edit = e;
          changed = true;
          continue;
        }
      }
      ls = le;
    }
    for (std::size_t i = 0; i < before.size();) {
      if (i >= edit.offset && i < edit.offset + edit.removed) {
        ++i;
        continue;
      }
      const std::string t = before.substr(0, i) + before.substr(i + 1);
      Edit e = edit;
      if (i < edit.offset) e.offset -= 1;
      if (same(t, e)) {
        before = t;
        edit = e;
        changed = true;
      } else {
        ++i;
      }
    }
    for (std::size_t i = 0; i < edit.inserted.size();) {
      Edit e = edit;
      e.inserted.erase(i, 1);
      if ((e.removed != 0 || !e.inserted.empty()) && same(before, e)) {
        edit = e;
        changed = true;
      } else {
        ++i;
      }
    }
    while (edit.removed > 0) {
      Edit e = edit;
      e.removed -= 1;
      const std::string t = before.substr(0, e.offset + e.removed) + before.substr(edit.offset + edit.removed);
      if ((e.removed == 0 && e.inserted.empty()) || !same(t, e)) break;
      before = t;
      edit = e;
      changed = true;
    }
  }
  return {before, edit};
}

int survey(const std::vector<lumos_test::EditBlock>& blocks, int per_kind) {
  std::map<std::string, int> counts;
  for (const auto& block : blocks) {
    std::string text = block.start_text;
    for (const Edit& edit : block.edits) {
      const std::string sig = signature(text, edit);
      if (!sig.empty() && counts[sig]++ < per_kind) {
        const auto [t, e] = shrink_keeping(text, edit, sig);
        std::cout << "[" << sig << "] {" << cpp_escape(t) << ", {" << e.offset << ", " << e.removed << ", "
                  << cpp_escape(e.inserted) << "}},\n"
                  << std::flush;
      }
      text.replace(edit.offset, edit.removed, edit.inserted);
    }
  }
  std::vector<std::pair<int, std::string>> sorted;
  int total = 0;
  for (const auto& [sig, n] : counts) {
    sorted.emplace_back(n, sig);
    total += n;
  }
  std::sort(sorted.rbegin(), sorted.rend());
  std::cerr << "failures " << total << "\n";
  for (const auto& [n, sig] : sorted) std::cerr << n << " " << sig << "\n";
  return total == 0 ? 0 : 1;
}

void print_failure(std::size_t block, std::size_t step, const std::string& before, const Edit& edit,
                    const Failure& f) {
  std::cerr << "block " << block << " step " << step << ": edit offset=" << edit.offset
            << " removed=" << edit.removed << " inserted=" << cpp_escape(edit.inserted) << "\n";
  if (f.is_fallback) std::cerr << "fallback (reason: " << f.detail << ")\n";
  else std::cerr << "mismatch:\n" << f.detail;
  std::cerr << lines_around(before, edit.offset, 15) << "\n";
}

}  

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: lumos_md_replay <dump-file> [--minimize]\n";
    return 2;
  }
  const std::string path = argv[1];
  bool minimize_flag = false;
  int survey_per_kind = -1;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--minimize") minimize_flag = true;
    if (a == "--survey") {
      survey_per_kind = 2;
      if (i + 1 < argc && std::isdigit(static_cast<unsigned char>(argv[i + 1][0])) != 0)
        survey_per_kind = std::atoi(argv[++i]);
    }
  }

  std::vector<lumos_test::EditBlock> blocks;
  try {
    blocks = lumos_test::read_dump_file(path);
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 2;
  }

  if (survey_per_kind >= 0) return survey(blocks, survey_per_kind);

  for (std::size_t b = 0; b < blocks.size(); ++b) {
    std::string text = blocks[b].start_text;
    for (std::size_t s = 0; s < blocks[b].edits.size(); ++s) {
      const Edit& edit = blocks[b].edits[s];
      const auto failure = check_edit(text, edit);
      if (failure) {
        print_failure(b, s, text, edit, *failure);
        if (minimize_flag) {
          const auto [min_text, min_edit] = minimize(text, edit, *failure);
          std::cerr << "\nminimized regression case:\n";
          std::cerr << "{" << cpp_escape(min_text) << ", {" << min_edit.offset << ", " << min_edit.removed << ", "
                     << cpp_escape(min_edit.inserted) << "}},\n";
        }
        return 1;
      }
      text.replace(edit.offset, edit.removed, edit.inserted);
    }
  }
  std::cerr << "no failure found in " << blocks.size() << " block(s)\n";
  return 0;
}
