#pragma once

#include <string>
#include <vector>

#include "toyc/diagnostic.h"
#include "toyc/token.h"

namespace toyc {

class Lexer {
 public:
  Lexer(std::string input, DiagnosticEngine& diagnostics);

  std::vector<Token> tokenize();

 private:
  bool isAtEnd() const;
  char peek() const;
  char peekNext() const;
  char advance();
  bool match(char expected);
  void skipWhitespaceAndComments();
  Token lexIdentifierOrKeyword();
  Token lexNumber();
  Token simpleToken(TokenKind kind, SourceLocation location, std::string lexeme);

  std::string input_;
  DiagnosticEngine& diagnostics_;
  std::size_t index_ = 0;
  std::size_t line_ = 1;
  std::size_t column_ = 1;
};

}  // namespace toyc
