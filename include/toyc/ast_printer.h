#pragma once

#include <ostream>

#include "toyc/ast.h"

namespace toyc {

class AstPrinter {
 public:
  void print(const ast::TranslationUnit& unit, std::ostream& out) const;
};

}  // namespace toyc
