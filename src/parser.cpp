#include "toyc/parser.h"

#include <memory>
#include <utility>
#include <vector>

namespace toyc {

namespace {

ast::BinaryOp tokenToBinaryOp(TokenKind kind) {
  switch (kind) {
    case TokenKind::PipePipe:
      return ast::BinaryOp::LogicalOr;
    case TokenKind::AmpAmp:
      return ast::BinaryOp::LogicalAnd;
    case TokenKind::Less:
      return ast::BinaryOp::Less;
    case TokenKind::Greater:
      return ast::BinaryOp::Greater;
    case TokenKind::LessEqual:
      return ast::BinaryOp::LessEqual;
    case TokenKind::GreaterEqual:
      return ast::BinaryOp::GreaterEqual;
    case TokenKind::EqualEqual:
      return ast::BinaryOp::Equal;
    case TokenKind::BangEqual:
      return ast::BinaryOp::NotEqual;
    case TokenKind::Plus:
      return ast::BinaryOp::Add;
    case TokenKind::Minus:
      return ast::BinaryOp::Subtract;
    case TokenKind::Star:
      return ast::BinaryOp::Multiply;
    case TokenKind::Slash:
      return ast::BinaryOp::Divide;
    case TokenKind::Percent:
      return ast::BinaryOp::Modulo;
    default:
      break;
  }
  return ast::BinaryOp::Add;
}

ast::UnaryOp tokenToUnaryOp(TokenKind kind) {
  switch (kind) {
    case TokenKind::Plus:
      return ast::UnaryOp::Plus;
    case TokenKind::Minus:
      return ast::UnaryOp::Minus;
    case TokenKind::Bang:
      return ast::UnaryOp::LogicalNot;
    default:
      break;
  }
  return ast::UnaryOp::Plus;
}

}  // namespace

Parser::Parser(std::vector<Token> tokens, DiagnosticEngine& diagnostics)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics) {}

std::unique_ptr<ast::TranslationUnit> Parser::parseTranslationUnit() {
  auto unit = std::make_unique<ast::TranslationUnit>(SourceLocation{});

  try {
    while (!isAtEnd()) {
      unit->items.push_back(parseTopLevelItem());
    }
  } catch (const ParseError&) {
    return nullptr;
  }

  return diagnostics_.hasErrors() ? nullptr : std::move(unit);
}

const Token& Parser::current() const { return tokens_[index_]; }

const Token& Parser::previous() const { return tokens_[index_ - 1]; }

const Token& Parser::lookahead(std::size_t offset) const {
  const std::size_t target = index_ + offset;
  return target < tokens_.size() ? tokens_[target] : tokens_.back();
}

bool Parser::isAtEnd() const { return current().kind == TokenKind::EndOfFile; }

bool Parser::check(TokenKind kind) const { return current().kind == kind; }

bool Parser::match(TokenKind kind) {
  if (!check(kind)) {
    return false;
  }
  advance();
  return true;
}

const Token& Parser::expect(TokenKind kind, const char* message) {
  if (check(kind)) {
    return advance();
  }
  errorHere(message);
}

const Token& Parser::advance() {
  if (!isAtEnd()) {
    ++index_;
  }
  return previous();
}

ast::TopLevelPtr Parser::parseTopLevelItem() {
  if (check(TokenKind::KwConst)) {
    return std::make_unique<ast::GlobalDecl>(current().location, parseDeclaration());
  }

  if (check(TokenKind::KwInt) && lookahead(1).kind == TokenKind::Identifier &&
      lookahead(2).kind == TokenKind::LParen) {
    return parseFunctionDefinition();
  }

  if (check(TokenKind::KwVoid)) {
    return parseFunctionDefinition();
  }

  if (check(TokenKind::KwInt)) {
    return std::make_unique<ast::GlobalDecl>(current().location, parseDeclaration());
  }

  errorHere("expected a top-level declaration or function definition");
}

ast::DeclPtr Parser::parseDeclaration() {
  const SourceLocation location = current().location;
  bool isConst = false;
  if (match(TokenKind::KwConst)) {
    isConst = true;
  }

  expect(TokenKind::KwInt, "expected 'int' in declaration");
  const Token& name = expect(TokenKind::Identifier, "expected identifier");
  expect(TokenKind::Assign, "expected '=' in declaration");
  ast::ExprPtr initializer = parseExpression();
  expect(TokenKind::Semicolon, "expected ';' after declaration");

  return std::make_unique<ast::Decl>(location, isConst, name.lexeme,
                                     std::move(initializer));
}

std::unique_ptr<ast::FuncDef> Parser::parseFunctionDefinition() {
  const SourceLocation location = current().location;
  bool returnsVoid = false;
  if (match(TokenKind::KwVoid)) {
    returnsVoid = true;
  } else {
    expect(TokenKind::KwInt, "expected 'int' or 'void' in function definition");
  }

  const Token& name = expect(TokenKind::Identifier, "expected function name");
  expect(TokenKind::LParen, "expected '(' after function name");

  std::vector<ast::Param> params;
  if (!check(TokenKind::RParen)) {
    do {
      expect(TokenKind::KwInt, "expected 'int' in parameter");
      const Token& paramName =
          expect(TokenKind::Identifier, "expected parameter name");
      params.push_back(ast::Param{paramName.location, paramName.lexeme});
    } while (match(TokenKind::Comma));
  }

  expect(TokenKind::RParen, "expected ')' after parameter list");
  auto body = parseBlock();
  return std::make_unique<ast::FuncDef>(location, returnsVoid, name.lexeme,
                                        std::move(params), std::move(body));
}

std::unique_ptr<ast::BlockStmt> Parser::parseBlock() {
  const SourceLocation location = current().location;
  expect(TokenKind::LBrace, "expected '{' to start block");

  auto block = std::make_unique<ast::BlockStmt>(location);
  while (!check(TokenKind::RBrace) && !isAtEnd()) {
    block->statements.push_back(parseStatement());
  }

  expect(TokenKind::RBrace, "expected '}' to close block");
  return block;
}

ast::StmtPtr Parser::parseStatement() {
  if (check(TokenKind::LBrace)) {
    return parseBlock();
  }

  if (match(TokenKind::Semicolon)) {
    return std::make_unique<ast::EmptyStmt>(previous().location);
  }

  if (check(TokenKind::KwConst) || check(TokenKind::KwInt)) {
    const SourceLocation location = current().location;
    return std::make_unique<ast::DeclStmt>(location, parseDeclaration());
  }

  if (match(TokenKind::KwIf)) {
    const SourceLocation location = previous().location;
    expect(TokenKind::LParen, "expected '(' after 'if'");
    ast::ExprPtr condition = parseExpression();
    expect(TokenKind::RParen, "expected ')' after if condition");
    ast::StmtPtr thenBranch = parseStatement();
    ast::StmtPtr elseBranch;
    if (match(TokenKind::KwElse)) {
      elseBranch = parseStatement();
    }
    return std::make_unique<ast::IfStmt>(location, std::move(condition),
                                         std::move(thenBranch),
                                         std::move(elseBranch));
  }

  if (match(TokenKind::KwWhile)) {
    const SourceLocation location = previous().location;
    expect(TokenKind::LParen, "expected '(' after 'while'");
    ast::ExprPtr condition = parseExpression();
    expect(TokenKind::RParen, "expected ')' after while condition");
    ast::StmtPtr body = parseStatement();
    return std::make_unique<ast::WhileStmt>(location, std::move(condition),
                                            std::move(body));
  }

  if (match(TokenKind::KwBreak)) {
    const SourceLocation location = previous().location;
    expect(TokenKind::Semicolon, "expected ';' after 'break'");
    return std::make_unique<ast::BreakStmt>(location);
  }

  if (match(TokenKind::KwContinue)) {
    const SourceLocation location = previous().location;
    expect(TokenKind::Semicolon, "expected ';' after 'continue'");
    return std::make_unique<ast::ContinueStmt>(location);
  }

  if (match(TokenKind::KwReturn)) {
    const SourceLocation location = previous().location;
    ast::ExprPtr value;
    if (!check(TokenKind::Semicolon)) {
      value = parseExpression();
    }
    expect(TokenKind::Semicolon, "expected ';' after 'return'");
    return std::make_unique<ast::ReturnStmt>(location, std::move(value));
  }

  if (check(TokenKind::Identifier) && lookahead(1).kind == TokenKind::Assign) {
    const SourceLocation location = current().location;
    const std::string name = advance().lexeme;
    advance();
    ast::ExprPtr value = parseExpression();
    expect(TokenKind::Semicolon, "expected ';' after assignment");
    return std::make_unique<ast::AssignStmt>(location, name, std::move(value));
  }

  const SourceLocation location = current().location;
  ast::ExprPtr expr = parseExpression();
  expect(TokenKind::Semicolon, "expected ';' after expression");
  return std::make_unique<ast::ExprStmt>(location, std::move(expr));
}

ast::ExprPtr Parser::parseExpression() { return parseLogicalOr(); }

ast::ExprPtr Parser::parseLogicalOr() {
  ast::ExprPtr expr = parseLogicalAnd();
  while (match(TokenKind::PipePipe)) {
    const Token op = previous();
    ast::ExprPtr rhs = parseLogicalAnd();
    expr = std::make_unique<ast::BinaryExpr>(op.location, tokenToBinaryOp(op.kind),
                                             std::move(expr), std::move(rhs));
  }
  return expr;
}

ast::ExprPtr Parser::parseLogicalAnd() {
  ast::ExprPtr expr = parseRelational();
  while (match(TokenKind::AmpAmp)) {
    const Token op = previous();
    ast::ExprPtr rhs = parseRelational();
    expr = std::make_unique<ast::BinaryExpr>(op.location, tokenToBinaryOp(op.kind),
                                             std::move(expr), std::move(rhs));
  }
  return expr;
}

ast::ExprPtr Parser::parseRelational() {
  ast::ExprPtr expr = parseAdditive();
  while (match(TokenKind::Less) || match(TokenKind::Greater) ||
         match(TokenKind::LessEqual) || match(TokenKind::GreaterEqual) ||
         match(TokenKind::EqualEqual) || match(TokenKind::BangEqual)) {
    const Token op = previous();
    ast::ExprPtr rhs = parseAdditive();
    expr = std::make_unique<ast::BinaryExpr>(op.location, tokenToBinaryOp(op.kind),
                                             std::move(expr), std::move(rhs));
  }
  return expr;
}

ast::ExprPtr Parser::parseAdditive() {
  ast::ExprPtr expr = parseMultiplicative();
  while (match(TokenKind::Plus) || match(TokenKind::Minus)) {
    const Token op = previous();
    ast::ExprPtr rhs = parseMultiplicative();
    expr = std::make_unique<ast::BinaryExpr>(op.location, tokenToBinaryOp(op.kind),
                                             std::move(expr), std::move(rhs));
  }
  return expr;
}

ast::ExprPtr Parser::parseMultiplicative() {
  ast::ExprPtr expr = parseUnary();
  while (match(TokenKind::Star) || match(TokenKind::Slash) ||
         match(TokenKind::Percent)) {
    const Token op = previous();
    ast::ExprPtr rhs = parseUnary();
    expr = std::make_unique<ast::BinaryExpr>(op.location, tokenToBinaryOp(op.kind),
                                             std::move(expr), std::move(rhs));
  }
  return expr;
}

ast::ExprPtr Parser::parseUnary() {
  if (match(TokenKind::Plus) || match(TokenKind::Minus) || match(TokenKind::Bang)) {
    const Token op = previous();
    ast::ExprPtr operand = parseUnary();
    return std::make_unique<ast::UnaryExpr>(op.location, tokenToUnaryOp(op.kind),
                                            std::move(operand));
  }
  return parsePrimary();
}

ast::ExprPtr Parser::parsePrimary() {
  if (match(TokenKind::Number)) {
    return std::make_unique<ast::IntegerLiteral>(
        previous().location, previous().intValue.value_or(0));
  }

  if (match(TokenKind::Identifier)) {
    const Token name = previous();
    if (match(TokenKind::LParen)) {
      std::vector<ast::ExprPtr> arguments;
      if (!check(TokenKind::RParen)) {
        do {
          arguments.push_back(parseExpression());
        } while (match(TokenKind::Comma));
      }
      expect(TokenKind::RParen, "expected ')' after argument list");
      return std::make_unique<ast::CallExpr>(name.location, name.lexeme,
                                             std::move(arguments));
    }
    return std::make_unique<ast::IdentifierExpr>(name.location, name.lexeme);
  }

  if (match(TokenKind::LParen)) {
    ast::ExprPtr expr = parseExpression();
    expect(TokenKind::RParen, "expected ')' after expression");
    return expr;
  }

  errorHere("expected expression");
}

[[noreturn]] void Parser::errorHere(const char* message) {
  diagnostics_.report(current().location, message);
  throw ParseError{};
}

}  // namespace toyc
