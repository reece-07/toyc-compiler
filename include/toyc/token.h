#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "toyc/source_location.h"

namespace toyc {

enum class TokenKind {
  EndOfFile,
  Identifier,
  Number,
  KwConst,
  KwInt,
  KwVoid,
  KwIf,
  KwElse,
  KwWhile,
  KwBreak,
  KwContinue,
  KwReturn,
  LParen,
  RParen,
  LBrace,
  RBrace,
  Comma,
  Semicolon,
  Assign,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Bang,
  Less,
  Greater,
  LessEqual,
  GreaterEqual,
  EqualEqual,
  BangEqual,
  AmpAmp,
  PipePipe
};

inline const char* toString(TokenKind kind) {
  switch (kind) {
    case TokenKind::EndOfFile:
      return "eof";
    case TokenKind::Identifier:
      return "identifier";
    case TokenKind::Number:
      return "number";
    case TokenKind::KwConst:
      return "const";
    case TokenKind::KwInt:
      return "int";
    case TokenKind::KwVoid:
      return "void";
    case TokenKind::KwIf:
      return "if";
    case TokenKind::KwElse:
      return "else";
    case TokenKind::KwWhile:
      return "while";
    case TokenKind::KwBreak:
      return "break";
    case TokenKind::KwContinue:
      return "continue";
    case TokenKind::KwReturn:
      return "return";
    case TokenKind::LParen:
      return "(";
    case TokenKind::RParen:
      return ")";
    case TokenKind::LBrace:
      return "{";
    case TokenKind::RBrace:
      return "}";
    case TokenKind::Comma:
      return ",";
    case TokenKind::Semicolon:
      return ";";
    case TokenKind::Assign:
      return "=";
    case TokenKind::Plus:
      return "+";
    case TokenKind::Minus:
      return "-";
    case TokenKind::Star:
      return "*";
    case TokenKind::Slash:
      return "/";
    case TokenKind::Percent:
      return "%";
    case TokenKind::Bang:
      return "!";
    case TokenKind::Less:
      return "<";
    case TokenKind::Greater:
      return ">";
    case TokenKind::LessEqual:
      return "<=";
    case TokenKind::GreaterEqual:
      return ">=";
    case TokenKind::EqualEqual:
      return "==";
    case TokenKind::BangEqual:
      return "!=";
    case TokenKind::AmpAmp:
      return "&&";
    case TokenKind::PipePipe:
      return "||";
  }
  return "<unknown>";
}

struct Token {
  TokenKind kind = TokenKind::EndOfFile;
  std::string lexeme;
  SourceLocation location;
  std::optional<std::int32_t> intValue;
};

}  // namespace toyc
