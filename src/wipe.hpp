#pragma once

#include <cstddef>
#include <string>

namespace lumos {

inline void wipe_bytes(char* p, std::size_t n) noexcept {
  volatile char* v = p;
  for (std::size_t i = 0; i < n; ++i) v[i] = 0;
}

inline void wipe_string(std::string& s) {
  s.resize(s.capacity());
  wipe_bytes(s.data(), s.size());
  s.clear();
  s.shrink_to_fit();
}

}  
