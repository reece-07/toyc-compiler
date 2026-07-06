#pragma once

#include "toyc/ast.h"
#include "toyc/diagnostic.h"

namespace toyc {

class SemanticAnalyzer {
 public:
  explicit SemanticAnalyzer(DiagnosticEngine& diagnostics);

  bool analyze(const ast::TranslationUnit& unit);

 private:
  DiagnosticEngine& diagnostics_;
};

}  // namespace toyc
