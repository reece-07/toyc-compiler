#pragma once

#include <ostream>

#include "toyc/ast.h"

namespace toyc {

class CodeGenerator {
 public:
  void emitProgram(const ast::TranslationUnit& unit, std::ostream& out) const;
};

}  // namespace toyc
