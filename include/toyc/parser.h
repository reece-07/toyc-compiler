#pragma once

#include <memory>
#include <vector>

#include "toyc/ast.h"
#include "toyc/diagnostic.h"
#include "toyc/token.h"

namespace toyc {

class Parser {
 public:
  Parser(std::vector<Token> tokens, DiagnosticEngine& diagnostics);

  std::unique_ptr<ast::TranslationUnit> parseTranslationUnit();

 private:
  class ParseError {};

  const Token& current() const;
  const Token& previous() const;
  const Token& lookahead(std::size_t offset) const;
  bool isAtEnd() const;
  bool check(TokenKind kind) const;
  bool match(TokenKind kind);
  const Token& expect(TokenKind kind, const char* message);
  const Token& advance();

  ast::TopLevelPtr parseTopLevelItem();
  ast::DeclPtr parseDeclaration();
  std::unique_ptr<ast::FuncDef> parseFunctionDefinition();
  std::unique_ptr<ast::BlockStmt> parseBlock();
  ast::StmtPtr parseStatement();

  ast::ExprPtr parseExpression();
  ast::ExprPtr parseLogicalOr();
  ast::ExprPtr parseLogicalAnd();
  ast::ExprPtr parseRelational();
  ast::ExprPtr parseAdditive();
  ast::ExprPtr parseMultiplicative();
  ast::ExprPtr parseUnary();
  ast::ExprPtr parsePrimary();

  [[noreturn]] void errorHere(const char* message);

  std::vector<Token> tokens_;
  DiagnosticEngine& diagnostics_;
  std::size_t index_ = 0;
};

}  // namespace toyc
