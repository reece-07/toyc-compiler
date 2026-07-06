#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "toyc/source_location.h"

namespace toyc::ast {

struct AstNode {
  explicit AstNode(SourceLocation location) : location(std::move(location)) {}
  virtual ~AstNode() = default;

  SourceLocation location;
};

struct Expr : AstNode {
  using AstNode::AstNode;
};

struct Stmt : AstNode {
  using AstNode::AstNode;
};

struct TopLevelItem : AstNode {
  using AstNode::AstNode;
};

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using TopLevelPtr = std::unique_ptr<TopLevelItem>;

enum class UnaryOp { Plus, Minus, LogicalNot };
enum class BinaryOp {
  LogicalOr,
  LogicalAnd,
  Less,
  Greater,
  LessEqual,
  GreaterEqual,
  Equal,
  NotEqual,
  Add,
  Subtract,
  Multiply,
  Divide,
  Modulo
};

struct Param {
  SourceLocation location;
  std::string name;
};

struct Decl : AstNode {
  Decl(SourceLocation location, bool isConst, std::string name, ExprPtr initializer)
      : AstNode(std::move(location)),
        isConst(isConst),
        name(std::move(name)),
        initializer(std::move(initializer)) {}

  bool isConst;
  std::string name;
  ExprPtr initializer;
};

using DeclPtr = std::unique_ptr<Decl>;

struct IntegerLiteral : Expr {
  IntegerLiteral(SourceLocation location, std::int32_t value)
      : Expr(std::move(location)), value(value) {}

  std::int32_t value;
};

struct IdentifierExpr : Expr {
  IdentifierExpr(SourceLocation location, std::string name)
      : Expr(std::move(location)), name(std::move(name)) {}

  std::string name;
};

struct UnaryExpr : Expr {
  UnaryExpr(SourceLocation location, UnaryOp op, ExprPtr operand)
      : Expr(std::move(location)), op(op), operand(std::move(operand)) {}

  UnaryOp op;
  ExprPtr operand;
};

struct BinaryExpr : Expr {
  BinaryExpr(SourceLocation location, BinaryOp op, ExprPtr lhs, ExprPtr rhs)
      : Expr(std::move(location)),
        op(op),
        lhs(std::move(lhs)),
        rhs(std::move(rhs)) {}

  BinaryOp op;
  ExprPtr lhs;
  ExprPtr rhs;
};

struct CallExpr : Expr {
  CallExpr(SourceLocation location, std::string callee, std::vector<ExprPtr> arguments)
      : Expr(std::move(location)),
        callee(std::move(callee)),
        arguments(std::move(arguments)) {}

  std::string callee;
  std::vector<ExprPtr> arguments;
};

struct BlockStmt : Stmt {
  explicit BlockStmt(SourceLocation location) : Stmt(std::move(location)) {}

  std::vector<StmtPtr> statements;
};

struct EmptyStmt : Stmt {
  using Stmt::Stmt;
};

struct ExprStmt : Stmt {
  ExprStmt(SourceLocation location, ExprPtr expr)
      : Stmt(std::move(location)), expr(std::move(expr)) {}

  ExprPtr expr;
};

struct AssignStmt : Stmt {
  AssignStmt(SourceLocation location, std::string name, ExprPtr value)
      : Stmt(std::move(location)),
        name(std::move(name)),
        value(std::move(value)) {}

  std::string name;
  ExprPtr value;
};

struct DeclStmt : Stmt {
  DeclStmt(SourceLocation location, DeclPtr decl)
      : Stmt(std::move(location)), decl(std::move(decl)) {}

  DeclPtr decl;
};

struct IfStmt : Stmt {
  IfStmt(SourceLocation location, ExprPtr condition, StmtPtr thenBranch,
         StmtPtr elseBranch)
      : Stmt(std::move(location)),
        condition(std::move(condition)),
        thenBranch(std::move(thenBranch)),
        elseBranch(std::move(elseBranch)) {}

  ExprPtr condition;
  StmtPtr thenBranch;
  StmtPtr elseBranch;
};

struct WhileStmt : Stmt {
  WhileStmt(SourceLocation location, ExprPtr condition, StmtPtr body)
      : Stmt(std::move(location)),
        condition(std::move(condition)),
        body(std::move(body)) {}

  ExprPtr condition;
  StmtPtr body;
};

struct BreakStmt : Stmt {
  using Stmt::Stmt;
};

struct ContinueStmt : Stmt {
  using Stmt::Stmt;
};

struct ReturnStmt : Stmt {
  ReturnStmt(SourceLocation location, ExprPtr value)
      : Stmt(std::move(location)), value(std::move(value)) {}

  ExprPtr value;
};

struct GlobalDecl : TopLevelItem {
  GlobalDecl(SourceLocation location, DeclPtr decl)
      : TopLevelItem(std::move(location)), decl(std::move(decl)) {}

  DeclPtr decl;
};

struct FuncDef : TopLevelItem {
  FuncDef(SourceLocation location, bool returnsVoid, std::string name,
          std::vector<Param> params, std::unique_ptr<BlockStmt> body)
      : TopLevelItem(std::move(location)),
        returnsVoid(returnsVoid),
        name(std::move(name)),
        params(std::move(params)),
        body(std::move(body)) {}

  bool returnsVoid;
  std::string name;
  std::vector<Param> params;
  std::unique_ptr<BlockStmt> body;
};

struct TranslationUnit : AstNode {
  explicit TranslationUnit(SourceLocation location) : AstNode(std::move(location)) {}

  std::vector<TopLevelPtr> items;
};

}  // namespace toyc::ast
