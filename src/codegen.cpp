#include "toyc/codegen.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace toyc {

namespace {

constexpr const char* kGlobalInitFunction = "__toyc_init_globals";
constexpr const char* kGlobalInitFlag = "__toyc_init_done";

int alignTo(int value, int alignment) {
  const int remainder = value % alignment;
  return remainder == 0 ? value : value + alignment - remainder;
}

int slotOffset(int slotIndex) { return -12 - slotIndex * 4; }

struct Storage {
  enum class Kind {
    Local,
    Global,
  };

  Kind kind = Kind::Local;
  int offset = 0;
  std::string label;
};

struct GlobalInfo {
  const ast::Decl* decl = nullptr;
  bool isConst = false;
  std::optional<std::int32_t> initialValue;
  bool needsRuntimeInit = false;
};

struct FunctionLayout {
  std::unordered_map<const ast::Decl*, int> declOffsets;
  std::vector<int> paramOffsets;
  int frameSize = 16;
};

class ConstantEvaluator {
 public:
  explicit ConstantEvaluator(
      const std::unordered_map<std::string, std::int32_t>& knownValues)
      : knownValues_(knownValues) {}

  std::optional<std::int32_t> evaluate(const ast::Expr& expr) const {
    if (const auto* literal = dynamic_cast<const ast::IntegerLiteral*>(&expr)) {
      return literal->value;
    }

    if (const auto* ident = dynamic_cast<const ast::IdentifierExpr*>(&expr)) {
      const auto it = knownValues_.find(ident->name);
      if (it == knownValues_.end()) {
        return std::nullopt;
      }
      return it->second;
    }

    if (const auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
      const auto operand = evaluate(*unary->operand);
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

    const auto lhs = evaluate(*binary->lhs);
    const auto rhs = evaluate(*binary->rhs);
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
          return std::nullopt;
        }
        return static_cast<std::int32_t>(*lhs / *rhs);
      case ast::BinaryOp::Modulo:
        if (*rhs == 0) {
          return std::nullopt;
        }
        return static_cast<std::int32_t>(*lhs % *rhs);
    }

    return std::nullopt;
  }

 private:
  const std::unordered_map<std::string, std::int32_t>& knownValues_;
};

void collectDeclSlots(const ast::Stmt& stmt, FunctionLayout& layout, int& slotIndex) {
  if (const auto* block = dynamic_cast<const ast::BlockStmt*>(&stmt)) {
    for (const auto& child : block->statements) {
      collectDeclSlots(*child, layout, slotIndex);
    }
    return;
  }

  if (const auto* declStmt = dynamic_cast<const ast::DeclStmt*>(&stmt)) {
    layout.declOffsets[declStmt->decl.get()] = slotOffset(slotIndex++);
    return;
  }

  if (const auto* ifStmt = dynamic_cast<const ast::IfStmt*>(&stmt)) {
    collectDeclSlots(*ifStmt->thenBranch, layout, slotIndex);
    if (ifStmt->elseBranch) {
      collectDeclSlots(*ifStmt->elseBranch, layout, slotIndex);
    }
    return;
  }

  if (const auto* whileStmt = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
    collectDeclSlots(*whileStmt->body, layout, slotIndex);
  }
}

FunctionLayout buildFunctionLayout(const ast::FuncDef& func) {
  FunctionLayout layout;
  int slotIndex = 0;

  layout.paramOffsets.reserve(func.params.size());
  for (std::size_t i = 0; i < func.params.size(); ++i) {
    layout.paramOffsets.push_back(slotOffset(slotIndex++));
  }

  for (const auto& statement : func.body->statements) {
    collectDeclSlots(*statement, layout, slotIndex);
  }

  layout.frameSize = alignTo(8 + slotIndex * 4, 16);
  return layout;
}

class GlobalInitEmitter {
 public:
  GlobalInitEmitter(const std::vector<const ast::Decl*>& decls,
                    const std::unordered_map<std::string, Storage>& globals,
                    std::ostream& out)
      : decls_(decls), globals_(globals), out_(out) {}

  void emit() {
    out_ << ".globl " << kGlobalInitFunction << '\n';
    out_ << kGlobalInitFunction << ":\n";
    out_ << "  addi sp, sp, -16\n";
    out_ << "  sw ra, 12(sp)\n";
    out_ << "  sw s0, 8(sp)\n";
    out_ << "  addi s0, sp, 16\n";

    for (const ast::Decl* decl : decls_) {
      emitExpr(*decl->initializer);
      storeGlobal(decl->name, "a0");
    }

    out_ << "  lw s0, 8(sp)\n";
    out_ << "  lw ra, 12(sp)\n";
    out_ << "  addi sp, sp, 16\n";
    out_ << "  ret\n\n";
  }

 private:
  void emitExpr(const ast::Expr& expr) {
    if (const auto* literal = dynamic_cast<const ast::IntegerLiteral*>(&expr)) {
      out_ << "  li a0, " << literal->value << '\n';
      return;
    }

    if (const auto* ident = dynamic_cast<const ast::IdentifierExpr*>(&expr)) {
      loadGlobal(ident->name, "a0");
      return;
    }

    if (const auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
      emitExpr(*unary->operand);
      switch (unary->op) {
        case ast::UnaryOp::Plus:
          break;
        case ast::UnaryOp::Minus:
          out_ << "  neg a0, a0\n";
          break;
        case ast::UnaryOp::LogicalNot:
          out_ << "  seqz a0, a0\n";
          break;
      }
      return;
    }

    if (const auto* call = dynamic_cast<const ast::CallExpr*>(&expr)) {
      emitCall(*call);
      return;
    }

    const auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr);
    if (binary == nullptr) {
      return;
    }

    if (binary->op == ast::BinaryOp::LogicalAnd ||
        binary->op == ast::BinaryOp::LogicalOr) {
      const std::string trueLabel = newLabel("logic_true");
      const std::string falseLabel = newLabel("logic_false");
      const std::string endLabel = newLabel("logic_end");
      emitCondition(expr, trueLabel, falseLabel);
      out_ << trueLabel << ":\n";
      out_ << "  li a0, 1\n";
      out_ << "  j " << endLabel << '\n';
      out_ << falseLabel << ":\n";
      out_ << "  li a0, 0\n";
      out_ << endLabel << ":\n";
      return;
    }

    emitExpr(*binary->lhs);
    pushRegister("a0");
    emitExpr(*binary->rhs);
    popRegister("t0");

    switch (binary->op) {
      case ast::BinaryOp::Add:
        out_ << "  add a0, t0, a0\n";
        break;
      case ast::BinaryOp::Subtract:
        out_ << "  sub a0, t0, a0\n";
        break;
      case ast::BinaryOp::Multiply:
        out_ << "  mul a0, t0, a0\n";
        break;
      case ast::BinaryOp::Divide:
        out_ << "  div a0, t0, a0\n";
        break;
      case ast::BinaryOp::Modulo:
        out_ << "  rem a0, t0, a0\n";
        break;
      case ast::BinaryOp::Less:
        out_ << "  slt a0, t0, a0\n";
        break;
      case ast::BinaryOp::Greater:
        out_ << "  slt a0, a0, t0\n";
        break;
      case ast::BinaryOp::LessEqual:
        out_ << "  slt a0, a0, t0\n";
        out_ << "  xori a0, a0, 1\n";
        break;
      case ast::BinaryOp::GreaterEqual:
        out_ << "  slt a0, t0, a0\n";
        out_ << "  xori a0, a0, 1\n";
        break;
      case ast::BinaryOp::Equal:
        out_ << "  sub t1, t0, a0\n";
        out_ << "  seqz a0, t1\n";
        break;
      case ast::BinaryOp::NotEqual:
        out_ << "  sub t1, t0, a0\n";
        out_ << "  snez a0, t1\n";
        break;
      case ast::BinaryOp::LogicalAnd:
      case ast::BinaryOp::LogicalOr:
        break;
    }
  }

  void emitCondition(const ast::Expr& expr, const std::string& trueLabel,
                     const std::string& falseLabel) {
    const auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr);
    if (binary != nullptr && binary->op == ast::BinaryOp::LogicalAnd) {
      const std::string rhsLabel = newLabel("and_rhs");
      emitCondition(*binary->lhs, rhsLabel, falseLabel);
      out_ << rhsLabel << ":\n";
      emitCondition(*binary->rhs, trueLabel, falseLabel);
      return;
    }

    if (binary != nullptr && binary->op == ast::BinaryOp::LogicalOr) {
      const std::string rhsLabel = newLabel("or_rhs");
      emitCondition(*binary->lhs, trueLabel, rhsLabel);
      out_ << rhsLabel << ":\n";
      emitCondition(*binary->rhs, trueLabel, falseLabel);
      return;
    }

    emitExpr(expr);
    out_ << "  bne a0, x0, " << trueLabel << '\n';
    out_ << "  j " << falseLabel << '\n';
  }

  void emitCall(const ast::CallExpr& call) {
    const std::size_t registerArgs = std::min<std::size_t>(call.arguments.size(), 8);
    const int extraArgsBytes =
        static_cast<int>((call.arguments.size() - registerArgs) * 4);
    const int padBytes = (16 - ((stackDepthBytes_ + extraArgsBytes) % 16)) % 16;

    if (padBytes > 0) {
      out_ << "  addi sp, sp, -" << padBytes << '\n';
      stackDepthBytes_ += padBytes;
    }

    for (std::size_t i = call.arguments.size(); i > 0; --i) {
      emitExpr(*call.arguments[i - 1]);
      pushRegister("a0");
    }

    for (std::size_t i = 0; i < registerArgs; ++i) {
      popRegister("a" + std::to_string(i));
    }

    out_ << "  call " << call.callee << '\n';

    if (extraArgsBytes + padBytes > 0) {
      out_ << "  addi sp, sp, " << extraArgsBytes + padBytes << '\n';
      stackDepthBytes_ -= extraArgsBytes + padBytes;
    }
  }

  void loadGlobal(const std::string& name, const std::string& targetReg) {
    const Storage storage = globals_.at(name);
    out_ << "  la t0, " << storage.label << '\n';
    out_ << "  lw " << targetReg << ", 0(t0)\n";
  }

  void storeGlobal(const std::string& name, const std::string& sourceReg) {
    const Storage storage = globals_.at(name);
    out_ << "  la t0, " << storage.label << '\n';
    out_ << "  sw " << sourceReg << ", 0(t0)\n";
  }

  void pushRegister(const std::string& reg) {
    out_ << "  addi sp, sp, -4\n";
    out_ << "  sw " << reg << ", 0(sp)\n";
    stackDepthBytes_ += 4;
  }

  void popRegister(const std::string& reg) {
    out_ << "  lw " << reg << ", 0(sp)\n";
    out_ << "  addi sp, sp, 4\n";
    stackDepthBytes_ -= 4;
  }

  std::string newLabel(const std::string& prefix) {
    return ".global_init_" + prefix + "_" + std::to_string(labelCounter_++);
  }

  const std::vector<const ast::Decl*>& decls_;
  const std::unordered_map<std::string, Storage>& globals_;
  std::ostream& out_;
  int labelCounter_ = 0;
  int stackDepthBytes_ = 0;
};

class FunctionEmitter {
 public:
  FunctionEmitter(const ast::FuncDef& func, const FunctionLayout& layout,
                  const std::unordered_map<std::string, Storage>& globals,
                  bool shouldInitGlobals, std::ostream& out)
      : func_(func),
        layout_(layout),
        globals_(globals),
        shouldInitGlobals_(shouldInitGlobals),
        out_(out) {}

  void emit() {
    exitLabel_ = newLabel("func_exit");

    out_ << ".globl " << func_.name << '\n';
    out_ << func_.name << ":\n";
    out_ << "  addi sp, sp, -" << layout_.frameSize << '\n';
    out_ << "  sw ra, " << layout_.frameSize - 4 << "(sp)\n";
    out_ << "  sw s0, " << layout_.frameSize - 8 << "(sp)\n";
    out_ << "  addi s0, sp, " << layout_.frameSize << '\n';

    pushScope();
    for (std::size_t i = 0; i < func_.params.size(); ++i) {
      const Storage storage{Storage::Kind::Local, layout_.paramOffsets[i], ""};
      declare(func_.params[i].name, storage);

      if (i < 8) {
        out_ << "  sw a" << i << ", " << storage.offset << "(s0)\n";
      } else {
        out_ << "  lw t0, " << static_cast<int>((i - 8) * 4) << "(s0)\n";
        out_ << "  sw t0, " << storage.offset << "(s0)\n";
      }
    }

    if (shouldInitGlobals_) {
      const std::string skipLabel = newLabel("globals_ready");
      out_ << "  la t0, " << kGlobalInitFlag << '\n';
      out_ << "  lw t1, 0(t0)\n";
      out_ << "  bne t1, x0, " << skipLabel << '\n';
      out_ << "  li t1, 1\n";
      out_ << "  sw t1, 0(t0)\n";
      out_ << "  call " << kGlobalInitFunction << '\n';
      out_ << skipLabel << ":\n";
    }

    emitBlock(*func_.body);

    out_ << exitLabel_ << ":\n";
    out_ << "  lw s0, " << layout_.frameSize - 8 << "(sp)\n";
    out_ << "  lw ra, " << layout_.frameSize - 4 << "(sp)\n";
    out_ << "  addi sp, sp, " << layout_.frameSize << "\n";
    out_ << "  ret\n\n";
    popScope();
  }

 private:
  void emitBlock(const ast::BlockStmt& block) {
    pushScope();
    for (const auto& statement : block.statements) {
      emitStmt(*statement);
    }
    popScope();
  }

  void emitStmt(const ast::Stmt& stmt) {
    if (const auto* block = dynamic_cast<const ast::BlockStmt*>(&stmt)) {
      emitBlock(*block);
      return;
    }

    if (dynamic_cast<const ast::EmptyStmt*>(&stmt) != nullptr) {
      return;
    }

    if (const auto* exprStmt = dynamic_cast<const ast::ExprStmt*>(&stmt)) {
      emitExpr(*exprStmt->expr);
      return;
    }

    if (const auto* declStmt = dynamic_cast<const ast::DeclStmt*>(&stmt)) {
      emitExpr(*declStmt->decl->initializer);
      const int offset = layout_.declOffsets.at(declStmt->decl.get());
      declare(declStmt->decl->name, Storage{Storage::Kind::Local, offset, ""});
      out_ << "  sw a0, " << offset << "(s0)\n";
      return;
    }

    if (const auto* assign = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
      emitExpr(*assign->value);
      storeName(assign->name, "a0");
      return;
    }

    if (const auto* ifStmt = dynamic_cast<const ast::IfStmt*>(&stmt)) {
      const std::string thenLabel = newLabel("if_then");
      const std::string elseLabel = newLabel("if_else");
      const std::string endLabel = newLabel("if_end");

      emitCondition(*ifStmt->condition, thenLabel,
                    ifStmt->elseBranch ? elseLabel : endLabel);
      out_ << thenLabel << ":\n";
      emitStmt(*ifStmt->thenBranch);
      if (ifStmt->elseBranch) {
        out_ << "  j " << endLabel << '\n';
        out_ << elseLabel << ":\n";
        emitStmt(*ifStmt->elseBranch);
      }
      out_ << endLabel << ":\n";
      return;
    }

    if (const auto* whileStmt = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
      const std::string condLabel = newLabel("while_cond");
      const std::string bodyLabel = newLabel("while_body");
      const std::string endLabel = newLabel("while_end");

      continueLabels_.push_back(condLabel);
      breakLabels_.push_back(endLabel);

      out_ << condLabel << ":\n";
      emitCondition(*whileStmt->condition, bodyLabel, endLabel);
      out_ << bodyLabel << ":\n";
      emitStmt(*whileStmt->body);
      out_ << "  j " << condLabel << '\n';
      out_ << endLabel << ":\n";

      continueLabels_.pop_back();
      breakLabels_.pop_back();
      return;
    }

    if (dynamic_cast<const ast::BreakStmt*>(&stmt) != nullptr) {
      out_ << "  j " << breakLabels_.back() << '\n';
      return;
    }

    if (dynamic_cast<const ast::ContinueStmt*>(&stmt) != nullptr) {
      out_ << "  j " << continueLabels_.back() << '\n';
      return;
    }

    const auto* returnStmt = dynamic_cast<const ast::ReturnStmt*>(&stmt);
    if (returnStmt == nullptr) {
      return;
    }

    if (returnStmt->value) {
      emitExpr(*returnStmt->value);
    }
    out_ << "  j " << exitLabel_ << '\n';
  }

  void emitExpr(const ast::Expr& expr) {
    if (const auto* literal = dynamic_cast<const ast::IntegerLiteral*>(&expr)) {
      out_ << "  li a0, " << literal->value << '\n';
      return;
    }

    if (const auto* ident = dynamic_cast<const ast::IdentifierExpr*>(&expr)) {
      loadName(ident->name, "a0");
      return;
    }

    if (const auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
      emitExpr(*unary->operand);
      switch (unary->op) {
        case ast::UnaryOp::Plus:
          break;
        case ast::UnaryOp::Minus:
          out_ << "  neg a0, a0\n";
          break;
        case ast::UnaryOp::LogicalNot:
          out_ << "  seqz a0, a0\n";
          break;
      }
      return;
    }

    if (const auto* call = dynamic_cast<const ast::CallExpr*>(&expr)) {
      emitCall(*call);
      return;
    }

    const auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr);
    if (binary == nullptr) {
      return;
    }

    if (binary->op == ast::BinaryOp::LogicalAnd ||
        binary->op == ast::BinaryOp::LogicalOr) {
      const std::string trueLabel = newLabel("logic_true");
      const std::string falseLabel = newLabel("logic_false");
      const std::string endLabel = newLabel("logic_end");
      emitCondition(expr, trueLabel, falseLabel);
      out_ << trueLabel << ":\n";
      out_ << "  li a0, 1\n";
      out_ << "  j " << endLabel << '\n';
      out_ << falseLabel << ":\n";
      out_ << "  li a0, 0\n";
      out_ << endLabel << ":\n";
      return;
    }

    emitExpr(*binary->lhs);
    pushRegister("a0");
    emitExpr(*binary->rhs);
    popRegister("t0");

    switch (binary->op) {
      case ast::BinaryOp::Add:
        out_ << "  add a0, t0, a0\n";
        break;
      case ast::BinaryOp::Subtract:
        out_ << "  sub a0, t0, a0\n";
        break;
      case ast::BinaryOp::Multiply:
        out_ << "  mul a0, t0, a0\n";
        break;
      case ast::BinaryOp::Divide:
        out_ << "  div a0, t0, a0\n";
        break;
      case ast::BinaryOp::Modulo:
        out_ << "  rem a0, t0, a0\n";
        break;
      case ast::BinaryOp::Less:
        out_ << "  slt a0, t0, a0\n";
        break;
      case ast::BinaryOp::Greater:
        out_ << "  slt a0, a0, t0\n";
        break;
      case ast::BinaryOp::LessEqual:
        out_ << "  slt a0, a0, t0\n";
        out_ << "  xori a0, a0, 1\n";
        break;
      case ast::BinaryOp::GreaterEqual:
        out_ << "  slt a0, t0, a0\n";
        out_ << "  xori a0, a0, 1\n";
        break;
      case ast::BinaryOp::Equal:
        out_ << "  sub t1, t0, a0\n";
        out_ << "  seqz a0, t1\n";
        break;
      case ast::BinaryOp::NotEqual:
        out_ << "  sub t1, t0, a0\n";
        out_ << "  snez a0, t1\n";
        break;
      case ast::BinaryOp::LogicalAnd:
      case ast::BinaryOp::LogicalOr:
        break;
    }
  }

  void emitCondition(const ast::Expr& expr, const std::string& trueLabel,
                     const std::string& falseLabel) {
    const auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr);
    if (binary != nullptr && binary->op == ast::BinaryOp::LogicalAnd) {
      const std::string rhsLabel = newLabel("and_rhs");
      emitCondition(*binary->lhs, rhsLabel, falseLabel);
      out_ << rhsLabel << ":\n";
      emitCondition(*binary->rhs, trueLabel, falseLabel);
      return;
    }

    if (binary != nullptr && binary->op == ast::BinaryOp::LogicalOr) {
      const std::string rhsLabel = newLabel("or_rhs");
      emitCondition(*binary->lhs, trueLabel, rhsLabel);
      out_ << rhsLabel << ":\n";
      emitCondition(*binary->rhs, trueLabel, falseLabel);
      return;
    }

    emitExpr(expr);
    out_ << "  bne a0, x0, " << trueLabel << '\n';
    out_ << "  j " << falseLabel << '\n';
  }

  void emitCall(const ast::CallExpr& call) {
    const std::size_t registerArgs = std::min<std::size_t>(call.arguments.size(), 8);
    const int extraArgsBytes =
        static_cast<int>((call.arguments.size() - registerArgs) * 4);
    const int padBytes = (16 - ((stackDepthBytes_ + extraArgsBytes) % 16)) % 16;

    if (padBytes > 0) {
      out_ << "  addi sp, sp, -" << padBytes << '\n';
      stackDepthBytes_ += padBytes;
    }

    for (std::size_t i = call.arguments.size(); i > 0; --i) {
      emitExpr(*call.arguments[i - 1]);
      pushRegister("a0");
    }

    for (std::size_t i = 0; i < registerArgs; ++i) {
      popRegister("a" + std::to_string(i));
    }

    out_ << "  call " << call.callee << '\n';

    if (extraArgsBytes + padBytes > 0) {
      out_ << "  addi sp, sp, " << extraArgsBytes + padBytes << '\n';
      stackDepthBytes_ -= extraArgsBytes + padBytes;
    }
  }

  void loadName(const std::string& name, const std::string& targetReg) {
    const Storage storage = lookup(name);
    if (storage.kind == Storage::Kind::Local) {
      out_ << "  lw " << targetReg << ", " << storage.offset << "(s0)\n";
      return;
    }

    out_ << "  la t0, " << storage.label << '\n';
    out_ << "  lw " << targetReg << ", 0(t0)\n";
  }

  void storeName(const std::string& name, const std::string& sourceReg) {
    const Storage storage = lookup(name);
    if (storage.kind == Storage::Kind::Local) {
      out_ << "  sw " << sourceReg << ", " << storage.offset << "(s0)\n";
      return;
    }

    out_ << "  la t0, " << storage.label << '\n';
    out_ << "  sw " << sourceReg << ", 0(t0)\n";
  }

  Storage lookup(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      const auto found = it->find(name);
      if (found != it->end()) {
        return found->second;
      }
    }

    const auto global = globals_.find(name);
    return global->second;
  }

  void pushRegister(const std::string& reg) {
    out_ << "  addi sp, sp, -4\n";
    out_ << "  sw " << reg << ", 0(sp)\n";
    stackDepthBytes_ += 4;
  }

  void popRegister(const std::string& reg) {
    out_ << "  lw " << reg << ", 0(sp)\n";
    out_ << "  addi sp, sp, 4\n";
    stackDepthBytes_ -= 4;
  }

  void pushScope() { scopes_.push_back({}); }

  void popScope() { scopes_.pop_back(); }

  void declare(const std::string& name, Storage storage) {
    scopes_.back()[name] = std::move(storage);
  }

  std::string newLabel(const std::string& prefix) {
    return "." + func_.name + "_" + prefix + "_" + std::to_string(labelCounter_++);
  }

  const ast::FuncDef& func_;
  const FunctionLayout& layout_;
  const std::unordered_map<std::string, Storage>& globals_;
  bool shouldInitGlobals_ = false;
  std::ostream& out_;
  std::vector<std::unordered_map<std::string, Storage>> scopes_;
  std::vector<std::string> breakLabels_;
  std::vector<std::string> continueLabels_;
  int labelCounter_ = 0;
  int stackDepthBytes_ = 0;
  std::string exitLabel_;
};

}  // namespace

void CodeGenerator::emitProgram(const ast::TranslationUnit& unit,
                                std::ostream& out) const {
  std::unordered_map<std::string, Storage> globals;
  std::unordered_map<std::string, GlobalInfo> globalInfo;
  std::unordered_map<std::string, std::int32_t> knownGlobalValues;
  std::vector<const ast::Decl*> dynamicGlobalDecls;

  for (const auto& item : unit.items) {
    const auto* global = dynamic_cast<const ast::GlobalDecl*>(item.get());
    if (global == nullptr) {
      continue;
    }

    globals.emplace(global->decl->name,
                    Storage{Storage::Kind::Global, 0, global->decl->name});

    ConstantEvaluator evaluator(knownGlobalValues);
    const auto initialValue = evaluator.evaluate(*global->decl->initializer);
    const bool needsRuntimeInit = !initialValue.has_value();
    globalInfo.emplace(global->decl->name,
                       GlobalInfo{global->decl.get(), global->decl->isConst,
                                  initialValue, needsRuntimeInit});

    if (initialValue.has_value()) {
      knownGlobalValues[global->decl->name] = *initialValue;
    } else {
      dynamicGlobalDecls.push_back(global->decl.get());
    }
  }

  const bool hasGlobals = !globalInfo.empty();
  const bool hasDynamicGlobalInit = !dynamicGlobalDecls.empty();

  out << "# RISC-V32 assembly generated by the ToyC compiler skeleton\n";
  out << "# Current milestone: globals, expressions, locals, control flow, and calls\n\n";

  if (hasGlobals || hasDynamicGlobalInit) {
    out << ".data\n";
    if (hasDynamicGlobalInit) {
      out << kGlobalInitFlag << ":\n";
      out << "  .word 0\n\n";
    }

    for (const auto& item : unit.items) {
      const auto* global = dynamic_cast<const ast::GlobalDecl*>(item.get());
      if (global == nullptr) {
        continue;
      }

      const auto info = globalInfo.find(global->decl->name);
      out << ".globl " << global->decl->name << '\n';
      out << global->decl->name << ":\n";
      if (info != globalInfo.end() && info->second.initialValue.has_value()) {
        out << "  .word " << *info->second.initialValue << "\n\n";
      } else {
        out << "  .word 0\n\n";
      }
    }
  }

  out << ".text\n";
  if (hasDynamicGlobalInit) {
    GlobalInitEmitter emitter(dynamicGlobalDecls, globals, out);
    emitter.emit();
  }

  for (const auto& item : unit.items) {
    const auto* func = dynamic_cast<const ast::FuncDef*>(item.get());
    if (func == nullptr) {
      continue;
    }

    const FunctionLayout layout = buildFunctionLayout(*func);
    FunctionEmitter emitter(*func, layout, globals,
                            hasDynamicGlobalInit && func->name == "main", out);
    emitter.emit();
  }
}

}  // namespace toyc
