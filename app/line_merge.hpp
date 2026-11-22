#pragma once

#include <string>
#include <string_view>
#include <vector>

std::vector<std::string_view> split_lines(std::string_view text);

struct LineHunk {
  int a_begin = 0, a_end = 0, b_begin = 0, b_end = 0;
};

inline constexpr int kMergeCap = 2000;

bool diff_lines(const std::vector<std::string_view>& a, const std::vector<std::string_view>& b, int cap,
                std::vector<LineHunk>* out);

enum class ChunkKind { same, yours, other, both_same, conflict };

struct MergeChunk {
  ChunkKind kind = ChunkKind::same;
  int base_begin = 0, base_end = 0;
  int yours_begin = 0, yours_end = 0;
  int other_begin = 0, other_end = 0;
};

struct LineMerge {
  std::vector<std::string_view> base, yours, other;
  std::vector<MergeChunk> chunks;
  bool too_many = false;  
  int conflicts() const;
};

LineMerge merge_lines(std::string_view base, std::string_view yours, std::string_view other);

enum class Pick { open, yours, other, both };

std::string merge_result(const LineMerge& merge, const std::vector<Pick>& picks);

std::vector<Pick> default_picks(const LineMerge& merge);
