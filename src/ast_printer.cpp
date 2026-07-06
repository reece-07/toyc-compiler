#include "toyc/ast_printer.h"

#include <ostream>
#include <string_view>

namespace toyc {

namespace {

std::string_view unaryOpName(ast::UnaryOp op) {
  switch (op) {
    case ast::UnaryOp::Plus:
      return "UnaryPlus";
    case ast::UnaryOp::Minus:
      return "UnaryMinus";
    case ast::UnaryOp::LogicalNot:
      return "LogicalNot";
  }
  return "Unary";
}

std::string_view binaryOpName(ast::BinaryOp op) {
  switch (op) {
    case ast::BinaryOp::LogicalOr:
      return "LogicalOr";
    case ast::BinaryOp::LogicalAnd:
      return "LogicalAnd";
    case ast::BinaryOp::Less:
      return "Less";
    case ast::BinaryOp::Greater:
      return "Greater";
    case ast::BinaryOp::LessEqual:
      return "LessEqual";
    case ast::BinaryOp::GreaterEqual:
      return "GreaterEqual";
    case ast::BinaryOp::Equal:
      return "Equal";
    case ast::BinaryOp::NotEqual:
      return "NotEqual";
    case ast::BinaryOp::Add:
      return "Add";
    case ast::BinaryOp::Subtract:
      return "Subtract";
    case ast::BinaryOp::Multiply:
      return "Multiply";
    case ast::BinaryOp::Divide:
      return "Divide";
    case ast::BinaryOp::Modulo:
      return "Modulo";
  }
  return "Binary";
}

void indent(std::ostream& out, int depth) {
  for (int i = 0; i < depth; ++i) {
    out << "  ";
  }
}

void printExpr(const ast::Expr& expr, std::ostream& out, int depth);
void printStmt(const ast::Stmt& stmt, std::ostream& out, int depth);

void printDecl(const ast::Decl& decl, std::ostream& out, int depth) {
  indent(out, depth);
  out << (decl.isConst ? "ConstDecl " : "VarDecl ") << decl.name << '\n';
  printExpr(*decl.initializer, out, depth + 1);
}

void printExpr(const ast::Expr& expr, std::ostream& out, int depth) {
  if (const auto* literal = dynamic_cast<const ast::IntegerLiteral*>(&expr)) {
    indent(out, depth);
    out << "Int " << literal->value << '\n';
    return;
  }

  if (const auto* ident = dynamic_cast<const ast::IdentifierExpr*>(&expr)) {
    indent(out, depth);
    out << "Identifier " << ident->name << '\n';
    return;
  }

  if (const auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
    indent(out, depth);
    out << unaryOpName(unary->op) << '\n';
    printExpr(*unary->operand, out, depth + 1);
    return;
  }

  if (const auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr)) {
    indent(out, depth);
    out << binaryOpName(binary->op) << '\n';
    printExpr(*binary->lhs, out, depth + 1);
    printExpr(*binary->rhs, out, depth + 1);
    return;
  }

  if (const auto* call = dynamic_cast<const ast::CallExpr*>(&expr)) {
    indent(out, depth);
    out << "Call " << call->callee << '\n';
    for (const auto& argument : call->arguments) {
      printExpr(*argument, out, depth + 1);
    }
  }
}

void printBlock(const ast::BlockStmt& block, std::ostream& out, int depth) {
  indent(out, depth);
  out << "Block\n";
  for (const auto& statement : block.statements) {
    printStmt(*statement, out, depth + 1);
  }
}

void printStmt(const ast::Stmt& stmt, std::ostream& out, int depth) {
  if (const auto* block = dynamic_cast<const ast::BlockStmt*>(&stmt)) {
    printBlock(*block, out, depth);
    return;
  }

  if (dynamic_cast<const ast::EmptyStmt*>(&stmt) != nullptr) {
    indent(out, depth);
    out << "EmptyStmt\n";
    return;
  }

  if (const auto* exprStmt = dynamic_cast<const ast::ExprStmt*>(&stmt)) {
    indent(out, depth);
    out << "ExprStmt\n";
    printExpr(*exprStmt->expr, out, depth + 1);
    return;
  }

  if (const auto* assign = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
    indent(out, depth);
    out << "Assign " << assign->name << '\n';
    printExpr(*assign->value, out, depth + 1);
    return;
  }

  if (const auto* decl = dynamic_cast<const ast::DeclStmt*>(&stmt)) {
    printDecl(*decl->decl, out, depth);
    return;
  }

  if (const auto* ifStmt = dynamic_cast<const ast::IfStmt*>(&stmt)) {
    indent(out, depth);
    out << "If\n";
    indent(out, depth + 1);
    out << "Condition\n";
    printExpr(*ifStmt->condition, out, depth + 2);
    indent(out, depth + 1);
    out << "Then\n";
    printStmt(*ifStmt->thenBranch, out, depth + 2);
    if (ifStmt->elseBranch) {
      indent(out, depth + 1);
      out << "Else\n";
      printStmt(*ifStmt->elseBranch, out, depth + 2);
    }
    return;
  }

  if (const auto* whileStmt = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
    indent(out, depth);
    out << "While\n";
    indent(out, depth + 1);
    out << "Condition\n";
    printExpr(*whileStmt->condition, out, depth + 2);
    indent(out, depth + 1);
    out << "Body\n";
    printStmt(*whileStmt->body, out, depth + 2);
    return;
  }

  if (dynamic_cast<const ast::BreakStmt*>(&stmt) != nullptr) {
    indent(out, depth);
    out << "Break\n";
    return;
  }

  if (dynamic_cast<const ast::ContinueStmt*>(&stmt) != nullptr) {
    indent(out, depth);
    out << "Continue\n";
    return;
  }

  if (const auto* ret = dynamic_cast<const ast::ReturnStmt*>(&stmt)) {
    indent(out, depth);
    out << "Return\n";
    if (ret->value) {
      printExpr(*ret->value, out, depth + 1);
    }
  }
}

}  // namespace

void AstPrinter::print(const ast::TranslationUnit& unit, std::ostream& out) const {
  out << "TranslationUnit\n";
  for (const auto& item : unit.items) {
    if (const auto* global = dynamic_cast<const ast::GlobalDecl*>(item.get())) {
      printDecl(*global->decl, out, 1);
      continue;
    }

    const auto* func = dynamic_cast<const ast::FuncDef*>(item.get());
    if (func == nullptr) {
      continue;
    }

    indent(out, 1);
    out << (func->returnsVoid ? "VoidFunc " : "IntFunc ") << func->name << '\n';
    for (const auto& param : func->params) {
      indent(out, 2);
      out << "Param " << param.name << '\n';
    }
    printBlock(*func->body, out, 2);
  }
}

}  // namespace toyc
