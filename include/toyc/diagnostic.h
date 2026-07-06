#pragma once

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "toyc/source_location.h"

namespace toyc {

struct Diagnostic {
  SourceLocation location;
  std::string message;
};

class DiagnosticEngine {
 public:
  void report(const SourceLocation& location, std::string message) {
    diagnostics_.push_back(Diagnostic{location, std::move(message)});
  }

  [[nodiscard]] bool hasErrors() const { return !diagnostics_.empty(); }

  [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const {
    return diagnostics_;
  }

  void print(std::ostream& out) const {
    for (const Diagnostic& diagnostic : diagnostics_) {
      out << diagnostic.location.line << ":" << diagnostic.location.column
          << ": error: " << diagnostic.message << '\n';
    }
  }

 private:
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace toyc
