# ToyC Compiler Skeleton

这是一个面向课程实践的 `ToyC -> RISC-V32` 编译器骨架，使用 `C++20` 和 `CMake` 搭建。

当前已经搭好的部分：

- 词法分析器：支持关键字、标识符、整数、注释、运算符和分隔符。
- 递归下降语法分析器：覆盖 PDF 里的主要文法，能构造 AST。
- AST 打印：本地调试语法树很方便。
- 语义分析骨架：检查一部分高频错误，例如重复定义、未声明使用、`main` 入口、`break/continue` 位置、`const` 赋值、函数调用顺序等。
- 代码生成骨架：当前会为每个函数输出一个占位版 RISC-V32 汇编壳，方便先把工程串起来。

还没完成的核心内容：

- 局部变量/全局变量的真正布局与寻址
- 表达式求值和控制流代码生成
- 调用约定、栈帧管理、参数传递
- 优化

## 构建

```bash
cmake -S . -B build
cmake --build build
```

如果你本地暂时没有 `cmake`，也可以直接用：

```bash
make
```

## 使用

标准评测接口：

```bash
./build/toyc < input.tc > output.s
```

本地调试辅助参数：

```bash
./build/toyc --dump-tokens < input.tc
./build/toyc --dump-ast < input.tc
./build/toyc -opt < input.tc > output.s
```

也支持本地直接读写文件：

```bash
./build/toyc --input samples/basic.tc --output output.s
./build/toyc --dump-ast --input samples/basic.tc
```

目前 `-opt` 只做参数兼容，还没有真正启用优化。

## 建议你下一步优先补的顺序

1. 先把 `semantic.cpp` 里还没覆盖到的语义规则补全。
2. 再把 `codegen.cpp` 从“函数空壳”替换成真正的表达式和语句生成。
3. 最后再考虑 IR、优化和寄存器分配。
