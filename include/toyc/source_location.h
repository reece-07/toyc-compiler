#pragma once

#include <cstddef>
#include <string>

namespace toyc {

struct SourceLocation {
  std::size_t offset = 0;
  std::size_t line = 1;
  std::size_t column = 1;
};

inline std::string formatLocation(const SourceLocation& location) {
  return std::to_string(location.line) + ":" + std::to_string(location.column);
}

}  // namespace toyc
