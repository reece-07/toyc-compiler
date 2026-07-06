cmake_minimum_required(VERSION 3.20)
project(toyc_compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(
  toyc_lib
  src/ast_printer.cpp
  src/codegen.cpp
  src/lexer.cpp
  src/parser.cpp
  src/semantic.cpp
)

target_include_directories(toyc_lib PUBLIC include)

target_compile_options(toyc_lib PRIVATE
  $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)

add_executable(toyc src/main.cpp)
target_link_libraries(toyc PRIVATE toyc_lib)
