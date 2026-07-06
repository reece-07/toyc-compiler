#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "toyc/ast_printer.h"
#include "toyc/codegen.h"
#include "toyc/diagnostic.h"
#include "toyc/lexer.h"
#include "toyc/parser.h"
#include "toyc/semantic.h"
#include "toyc/token.h"

namespace {

void dumpTokens(const std::vector<toyc::Token>& tokens) {
  for (const auto& token : tokens) {
    std::cout << token.location.line << ":" << token.location.column << "  "
              << toyc::toString(token.kind);
    if (!token.lexeme.empty()) {
      std::cout << "  " << token.lexeme;
    }
    std::cout << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  bool dumpAst = false;
  bool dumpTokensFlag = false;
  std::string inputPath;
  std::string outputPath;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--dump-ast") {
      dumpAst = true;
      continue;
    }
    if (arg == "--dump-tokens") {
      dumpTokensFlag = true;
      continue;
    }
    if (arg == "-opt") {
      continue;
    }
    if (arg == "--input") {
      if (i + 1 >= argc) {
        std::cerr << "missing file path after --input\n";
        return 1;
      }
      inputPath = argv[++i];
      continue;
    }
    if (arg == "--output") {
      if (i + 1 >= argc) {
        std::cerr << "missing file path after --output\n";
        return 1;
      }
      outputPath = argv[++i];
      continue;
    }

    std::cerr << "unknown argument: " << arg << '\n';
    return 1;
  }

  std::ifstream inputFile;
  std::istream* inputStream = &std::cin;
  if (!inputPath.empty()) {
    inputFile.open(inputPath);
    if (!inputFile) {
      std::cerr << "failed to open input file: " << inputPath << '\n';
      return 1;
    }
    inputStream = &inputFile;
  }

  std::ofstream outputFile;
  std::ostream* outputStream = &std::cout;
  if (!outputPath.empty()) {
    outputFile.open(outputPath);
    if (!outputFile) {
      std::cerr << "failed to open output file: " << outputPath << '\n';
      return 1;
    }
    outputStream = &outputFile;
  }

  const std::string input{std::istreambuf_iterator<char>(*inputStream),
                          std::istreambuf_iterator<char>()};

  toyc::DiagnosticEngine diagnostics;
  toyc::Lexer lexer(input, diagnostics);
  std::vector<toyc::Token> tokens = lexer.tokenize();
  if (diagnostics.hasErrors()) {
    diagnostics.print(std::cerr);
    return 1;
  }

  if (dumpTokensFlag) {
    dumpTokens(tokens);
    return 0;
  }

  toyc::Parser parser(tokens, diagnostics);
  auto unit = parser.parseTranslationUnit();
  if (diagnostics.hasErrors() || unit == nullptr) {
    diagnostics.print(std::cerr);
    return 1;
  }

  if (dumpAst) {
    toyc::AstPrinter printer;
    printer.print(*unit, *outputStream);
    return 0;
  }

  toyc::SemanticAnalyzer semantic(diagnostics);
  if (!semantic.analyze(*unit)) {
    diagnostics.print(std::cerr);
    return 1;
  }

  toyc::CodeGenerator codegen;
  codegen.emitProgram(*unit, *outputStream);
  return 0;
}
