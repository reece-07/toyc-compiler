#include "toyc/semantic.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace toyc {

namespace {

struct SymbolInfo {
  bool isConst = false;
  std::optional<std::int32_t> constValue;
};

struct FunctionInfo {
  bool returnsVoid = false;
  std::size_t paramCount = 0;
};

enum class ExprContext {
  Value,
  Condition,
  ExprStmtRoot,
};

class Analyzer {
 public:
  explicit Analyzer(DiagnosticEngine& diagnostics) : diagnostics_(diagnostics) {}

  bool analyze(const ast::TranslationUnit& unit) {
    scopes_.push_back({});

    bool hasMain = false;
    bool mainIsValid = false;

    for (const auto& item : unit.items) {
      if (const auto* global = dynamic_cast<const ast::GlobalDecl*>(item.get())) {
        analyzeGlobalDecl(*global->decl);
        continue;
      }

      const auto* func = dynamic_cast<const ast::FuncDef*>(item.get());
      if (func == nullptr) {
        continue;
      }

      if (!declareGlobalName(func->name, func->location)) {
        continue;
      }

      functions_[func->name] = FunctionInfo{func->returnsVoid, func->params.size()};

      if (func->name == "main") {
        hasMain = true;
        mainIsValid = !func->returnsVoid && func->params.empty();
      }

      analyzeFunction(*func);
    }

    if (!hasMain) {
      diagnostics_.report(unit.location, "program must define 'int main()'");
    } else if (!mainIsValid) {
      diagnostics_.report(unit.location, "'main' must have signature 'int main()'");
    }

    return !diagnostics_.hasErrors();
  }

 private:
  bool declareGlobalName(const std::string& name, const SourceLocation& location) {
    const auto [_, inserted] = globalNames_.emplace(name, location);
    if (!inserted) {
      diagnostics_.report(location, "redefinition of '" + name + "'");
      return false;
    }
    return true;
  }

  bool declareLocalName(const std::string& name, const SourceLocation& location,
                        SymbolInfo info) {
    auto& scope = scopes_.back();
    const auto [_, inserted] = scope.emplace(name, std::move(info));
    if (!inserted) {
      diagnostics_.report(location, "duplicate declaration of '" + name + "'");
      return false;
    }
    return true;
  }

  const SymbolInfo* lookupSymbol(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      const auto found = it->find(name);
      if (found != it->end()) {
        return &found->second;
      }
    }
    return nullptr;
  }

  void analyzeGlobalDecl(const ast::Decl& decl) {
    analyzeExpr(*decl.initializer, ExprContext::Value);

    SymbolInfo info;
    info.isConst = decl.isConst;
    if (decl.isConst) {
      info.constValue = evaluateConstExpr(*decl.initializer);
      if (!info.constValue.has_value()) {
        diagnostics_.report(
            decl.location,
            "const initializer must be a compile-time expression using constants");
      }
    }

    if (!declareGlobalName(decl.name, decl.location)) {
      return;
    }
    scopes_.front()[decl.name] = std::move(info);
  }

  void analyzeFunction(const ast::FuncDef& func) {
    currentFunction_ = &func;

    scopes_.push_back({});
    for (const auto& param : func.params) {
      declareLocalName(param.name, param.location, SymbolInfo{false, std::nullopt});
    }

    analyzeBlockStatements(*func.body);

    if (!func.returnsVoid && !guaranteesReturn(*func.body)) {
      diagnostics_.report(func.location,
                          "int function '" + func.name +
                              "' does not return on every control path");
    }

    scopes_.pop_back();
    currentFunction_ = nullptr;
  }

  void analyzeBlockStatements(const ast::BlockStmt& block) {
    for (const auto& statement : block.statements) {
      analyzeStmt(*statement);
    }
  }

  void analyzeNestedBlock(const ast::BlockStmt& block) {
    scopes_.push_back({});
    analyzeBlockStatements(block);
    scopes_.pop_back();
  }

  void analyzeStmt(const ast::Stmt& stmt) {
    if (const auto* block = dynamic_cast<const ast::BlockStmt*>(&stmt)) {
      analyzeNestedBlock(*block);
      return;
    }

    if (dynamic_cast<const ast::EmptyStmt*>(&stmt) != nullptr) {
      return;
    }

    if (const auto* exprStmt = dynamic_cast<const ast::ExprStmt*>(&stmt)) {
      analyzeExpr(*exprStmt->expr, ExprContext::ExprStmtRoot);
      return;
    }

    if (const auto* assign = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
      const SymbolInfo* symbol = lookupSymbol(assign->name);
      if (symbol == nullptr) {
        diagnostics_.report(assign->location,
                            "assignment to undeclared identifier '" + assign->name +
                                "'");
      } else if (symbol->isConst) {
        diagnostics_.report(assign->location,
                            "cannot assign to const identifier '" + assign->name + "'");
      }
      analyzeExpr(*assign->value, ExprContext::Value);
      return;
    }

    if (const auto* decl = dynamic_cast<const ast::DeclStmt*>(&stmt)) {
      analyzeExpr(*decl->decl->initializer, ExprContext::Value);

      SymbolInfo info;
      info.isConst = decl->decl->isConst;
      if (decl->decl->isConst) {
        info.constValue = evaluateConstExpr(*decl->decl->initializer);
        if (!info.constValue.has_value()) {
          diagnostics_.report(
              decl->location,
              "const initializer must be a compile-time expression using constants");
        }
      }

      declareLocalName(decl->decl->name, decl->decl->location, std::move(info));
      return;
    }

    if (const auto* ifStmt = dynamic_cast<const ast::IfStmt*>(&stmt)) {
      analyzeExpr(*ifStmt->condition, ExprContext::Condition);
      analyzeStmt(*ifStmt->thenBranch);
      if (ifStmt->elseBranch) {
        analyzeStmt(*ifStmt->elseBranch);
      }
      return;
    }

    if (const auto* whileStmt = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
      analyzeExpr(*whileStmt->condition, ExprContext::Condition);
      ++loopDepth_;
      analyzeStmt(*whileStmt->body);
      --loopDepth_;
      return;
    }

    if (const auto* breakStmt = dynamic_cast<const ast::BreakStmt*>(&stmt)) {
      if (loopDepth_ == 0) {
        diagnostics_.report(breakStmt->location,
                            "'break' can only appear inside a loop");
      }
      return;
    }

    if (const auto* continueStmt = dynamic_cast<const ast::ContinueStmt*>(&stmt)) {
      if (loopDepth_ == 0) {
        diagnostics_.report(continueStmt->location,
                            "'continue' can only appear inside a loop");
      }
      return;
    }

    if (const auto* returnStmt = dynamic_cast<const ast::ReturnStmt*>(&stmt)) {
      if (currentFunction_ == nullptr) {
        return;
      }

      if (currentFunction_->returnsVoid) {
        if (returnStmt->value) {
          diagnostics_.report(returnStmt->location,
                              "void function cannot return a value");
          analyzeExpr(*returnStmt->value, ExprContext::Value);
        }
      } else {
        if (!returnStmt->value) {
          diagnostics_.report(returnStmt->location,
                              "int function must return a value");
        } else {
          analyzeExpr(*returnStmt->value, ExprContext::Value);
        }
      }
      return;
    }
  }

  void analyzeExpr(const ast::Expr& expr, ExprContext context) {
    if (dynamic_cast<const ast::IntegerLiteral*>(&expr) != nullptr) {
      return;
    }

    if (const auto* ident = dynamic_cast<const ast::IdentifierExpr*>(&expr)) {
      if (lookupSymbol(ident->name) == nullptr) {
        diagnostics_.report(ident->location,
                            "use of undeclared identifier '" + ident->name + "'");
      }
      return;
    }

    if (const auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
      analyzeExpr(*unary->operand, ExprContext::Value);
      return;
    }

    if (const auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr)) {
      analyzeExpr(*binary->lhs, ExprContext::Value);
      analyzeExpr(*binary->rhs, ExprContext::Value);
      return;
    }

    const auto* call = dynamic_cast<const ast::CallExpr*>(&expr);
    if (call == nullptr) {
      return;
    }

    const auto found = functions_.find(call->callee);
    if (found == functions_.end()) {
      diagnostics_.report(call->location,
                          "call to function '" + call->callee +
                              "' before its definition");
      for (const auto& argument : call->arguments) {
        analyzeExpr(*argument, ExprContext::Value);
      }
      return;
    }

    const FunctionInfo& function = found->second;
    if (call->arguments.size() != function.paramCount) {
      diagnostics_.report(
          call->location,
          "function '" + call->callee + "' expects " +
              std::to_string(function.paramCount) + " argument(s), got " +
              std::to_string(call->arguments.size()));
    }

    for (const auto& argument : call->arguments) {
      analyzeExpr(*argument, ExprContext::Value);
    }

    if (function.returnsVoid && context != ExprContext::ExprStmtRoot) {
      diagnostics_.report(call->location,
                          "void function call cannot be used as a value");
    }
  }

  std::optional<std::int32_t> evaluateConstExpr(const ast::Expr& expr) {
    if (const auto* literal = dynamic_cast<const ast::IntegerLiteral*>(&expr)) {
      return literal->value;
    }

    if (const auto* ident = dynamic_cast<const ast::IdentifierExpr*>(&expr)) {
      const SymbolInfo* symbol = lookupSymbol(ident->name);
      if (symbol == nullptr || !symbol->isConst || !symbol->constValue.has_value()) {
        return std::nullopt;
      }
      return symbol->constValue;
    }

    if (const auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
      const auto operand = evaluateConstExpr(*unary->operand);
      if (!operand.has_value()) {
        return std::nullopt;
      }

      switch (unary->op) {
        case ast::UnaryOp::Plus:
          return *operand;
        case ast::UnaryOp::Minus:
          return static_cast<std::int32_t>(-*operand);
        case ast::UnaryOp::LogicalNot:
          return static_cast<std::int32_t>(*operand == 0 ? 1 : 0);
      }
    }

    const auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr);
    if (binary == nullptr) {
      return std::nullopt;
    }

    const auto lhs = evaluateConstExpr(*binary->lhs);
    const auto rhs = evaluateConstExpr(*binary->rhs);
    if (!lhs.has_value() || !rhs.has_value()) {
      return std::nullopt;
    }

    switch (binary->op) {
      case ast::BinaryOp::LogicalOr:
        return static_cast<std::int32_t>((*lhs != 0) || (*rhs != 0));
      case ast::BinaryOp::LogicalAnd:
        return static_cast<std::int32_t>((*lhs != 0) && (*rhs != 0));
      case ast::BinaryOp::Less:
        return static_cast<std::int32_t>(*lhs < *rhs);
      case ast::BinaryOp::Greater:
        return static_cast<std::int32_t>(*lhs > *rhs);
      case ast::BinaryOp::LessEqual:
        return static_cast<std::int32_t>(*lhs <= *rhs);
      case ast::BinaryOp::GreaterEqual:
        return static_cast<std::int32_t>(*lhs >= *rhs);
      case ast::BinaryOp::Equal:
        return static_cast<std::int32_t>(*lhs == *rhs);
      case ast::BinaryOp::NotEqual:
        return static_cast<std::int32_t>(*lhs != *rhs);
      case ast::BinaryOp::Add:
        return static_cast<std::int32_t>(*lhs + *rhs);
      case ast::BinaryOp::Subtract:
        return static_cast<std::int32_t>(*lhs - *rhs);
      case ast::BinaryOp::Multiply:
        return static_cast<std::int32_t>(*lhs * *rhs);
      case ast::BinaryOp::Divide:
        if (*rhs == 0) {
          diagnostics_.report(expr.location, "division by zero in const expression");
          return std::nullopt;
        }
        return static_cast<std::int32_t>(*lhs / *rhs);
      case ast::BinaryOp::Modulo:
        if (*rhs == 0) {
          diagnostics_.report(expr.location, "modulo by zero in const expression");
          return std::nullopt;
        }
        return static_cast<std::int32_t>(*lhs % *rhs);
    }

    return std::nullopt;
  }

  bool guaranteesReturn(const ast::Stmt& stmt) const {
    if (dynamic_cast<const ast::ReturnStmt*>(&stmt) != nullptr) {
      return true;
    }

    if (const auto* block = dynamic_cast<const ast::BlockStmt*>(&stmt)) {
      for (const auto& statement : block->statements) {
        if (guaranteesReturn(*statement)) {
          return true;
        }
      }
      return false;
    }

    const auto* ifStmt = dynamic_cast<const ast::IfStmt*>(&stmt);
    if (ifStmt == nullptr) {
      return false;
    }

    return ifStmt->elseBranch != nullptr &&
           guaranteesReturn(*ifStmt->thenBranch) &&
           guaranteesReturn(*ifStmt->elseBranch);
  }

  DiagnosticEngine& diagnostics_;
  std::unordered_map<std::string, SourceLocation> globalNames_;
  std::unordered_map<std::string, FunctionInfo> functions_;
  std::vector<std::unordered_map<std::string, SymbolInfo>> scopes_;
  const ast::FuncDef* currentFunction_ = nullptr;
  int loopDepth_ = 0;
};

}  // namespace

SemanticAnalyzer::SemanticAnalyzer(DiagnosticEngine& diagnostics)
    : diagnostics_(diagnostics) {}

bool SemanticAnalyzer::analyze(const ast::TranslationUnit& unit) {
  Analyzer analyzer(diagnostics_);
  return analyzer.analyze(unit);
}

}  // namespace toyc
