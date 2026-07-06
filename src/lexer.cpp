#include "toyc/lexer.h"

#include <cctype>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace toyc {

namespace {

bool isIdentifierStart(char ch) {
  return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool isIdentifierPart(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

}  // namespace

Lexer::Lexer(std::string input, DiagnosticEngine& diagnostics)
    : input_(std::move(input)), diagnostics_(diagnostics) {}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;

  while (!isAtEnd()) {
    skipWhitespaceAndComments();
    if (isAtEnd()) {
      break;
    }

    const SourceLocation location{index_, line_, column_};
    const char ch = peek();

    if (isIdentifierStart(ch)) {
      tokens.push_back(lexIdentifierOrKeyword());
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      tokens.push_back(lexNumber());
      continue;
    }

    advance();
    switch (ch) {
      case '(':
        tokens.push_back(simpleToken(TokenKind::LParen, location, "("));
        break;
      case ')':
        tokens.push_back(simpleToken(TokenKind::RParen, location, ")"));
        break;
      case '{':
        tokens.push_back(simpleToken(TokenKind::LBrace, location, "{"));
        break;
      case '}':
        tokens.push_back(simpleToken(TokenKind::RBrace, location, "}"));
        break;
      case ',':
        tokens.push_back(simpleToken(TokenKind::Comma, location, ","));
        break;
      case ';':
        tokens.push_back(simpleToken(TokenKind::Semicolon, location, ";"));
        break;
      case '+':
        tokens.push_back(simpleToken(TokenKind::Plus, location, "+"));
        break;
      case '-':
        tokens.push_back(simpleToken(TokenKind::Minus, location, "-"));
        break;
      case '*':
        tokens.push_back(simpleToken(TokenKind::Star, location, "*"));
        break;
      case '%':
        tokens.push_back(simpleToken(TokenKind::Percent, location, "%"));
        break;
      case '/':
        tokens.push_back(simpleToken(TokenKind::Slash, location, "/"));
        break;
      case '!':
        if (match('=')) {
          tokens.push_back(simpleToken(TokenKind::BangEqual, location, "!="));
        } else {
          tokens.push_back(simpleToken(TokenKind::Bang, location, "!"));
        }
        break;
      case '=':
        if (match('=')) {
          tokens.push_back(simpleToken(TokenKind::EqualEqual, location, "=="));
        } else {
          tokens.push_back(simpleToken(TokenKind::Assign, location, "="));
        }
        break;
      case '<':
        if (match('=')) {
          tokens.push_back(simpleToken(TokenKind::LessEqual, location, "<="));
        } else {
          tokens.push_back(simpleToken(TokenKind::Less, location, "<"));
        }
        break;
      case '>':
        if (match('=')) {
          tokens.push_back(simpleToken(TokenKind::GreaterEqual, location, ">="));
        } else {
          tokens.push_back(simpleToken(TokenKind::Greater, location, ">"));
        }
        break;
      case '&':
        if (match('&')) {
          tokens.push_back(simpleToken(TokenKind::AmpAmp, location, "&&"));
        } else {
          diagnostics_.report(location, "unexpected character '&'");
        }
        break;
      case '|':
        if (match('|')) {
          tokens.push_back(simpleToken(TokenKind::PipePipe, location, "||"));
        } else {
          diagnostics_.report(location, "unexpected character '|'");
        }
        break;
      default:
        diagnostics_.report(location,
                            std::string("unexpected character '") + ch + "'");
        break;
    }
  }

  tokens.push_back(Token{TokenKind::EndOfFile, "", SourceLocation{index_, line_, column_},
                         std::nullopt});
  return tokens;
}

bool Lexer::isAtEnd() const { return index_ >= input_.size(); }

char Lexer::peek() const { return isAtEnd() ? '\0' : input_[index_]; }

char Lexer::peekNext() const {
  return index_ + 1 >= input_.size() ? '\0' : input_[index_ + 1];
}

char Lexer::advance() {
  const char ch = input_[index_++];
  if (ch == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  return ch;
}

bool Lexer::match(char expected) {
  if (isAtEnd() || input_[index_] != expected) {
    return false;
  }
  advance();
  return true;
}

void Lexer::skipWhitespaceAndComments() {
  while (!isAtEnd()) {
    const char ch = peek();
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      advance();
      continue;
    }

    if (ch == '/' && peekNext() == '/') {
      advance();
      advance();
      while (!isAtEnd() && peek() != '\n') {
        advance();
      }
      continue;
    }

    if (ch == '/' && peekNext() == '*') {
      const SourceLocation commentStart{index_, line_, column_};
      bool terminated = false;
      advance();
      advance();
      while (!isAtEnd()) {
        if (peek() == '*' && peekNext() == '/') {
          advance();
          advance();
          terminated = true;
          break;
        }
        advance();
      }
      if (!terminated) {
        diagnostics_.report(commentStart, "unterminated block comment");
      }
      continue;
    }

    break;
  }
}

Token Lexer::lexIdentifierOrKeyword() {
  const SourceLocation location{index_, line_, column_};
  std::string lexeme;

  while (!isAtEnd() && isIdentifierPart(peek())) {
    lexeme.push_back(advance());
  }

  static const std::unordered_map<std::string, TokenKind> keywords{
      {"const", TokenKind::KwConst},       {"int", TokenKind::KwInt},
      {"void", TokenKind::KwVoid},         {"if", TokenKind::KwIf},
      {"else", TokenKind::KwElse},         {"while", TokenKind::KwWhile},
      {"break", TokenKind::KwBreak},       {"continue", TokenKind::KwContinue},
      {"return", TokenKind::KwReturn},
  };

  if (const auto it = keywords.find(lexeme); it != keywords.end()) {
    return Token{it->second, lexeme, location, std::nullopt};
  }
  return Token{TokenKind::Identifier, lexeme, location, std::nullopt};
}

Token Lexer::lexNumber() {
  const SourceLocation location{index_, line_, column_};
  std::string lexeme;

  while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
    lexeme.push_back(advance());
  }

  std::int64_t value = 0;
  for (char ch : lexeme) {
    value = value * 10 + (ch - '0');
    if (value > std::numeric_limits<std::int32_t>::max()) {
      diagnostics_.report(location, "integer literal out of range");
      value = std::numeric_limits<std::int32_t>::max();
      break;
    }
  }

  return Token{TokenKind::Number, lexeme, location,
               static_cast<std::int32_t>(value)};
}

Token Lexer::simpleToken(TokenKind kind, SourceLocation location,
                         std::string lexeme) {
  return Token{kind, std::move(lexeme), location, std::nullopt};
}

}  // namespace toyc
