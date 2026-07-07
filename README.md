# ToyC Compiler Skeleton

这是一个面向课程实践的 `ToyC -> RISC-V32` 编译器骨架，使用 `C++20` 和 `CMake` 搭建。

当前已经搭好的部分：

- 词法分析器：支持关键字、标识符、整数、注释、运算符和分隔符。
- 递归下降语法分析器：覆盖 PDF 里的主要文法，能构造 AST。
- AST 打印：本地调试语法树很方便。
- 语义分析骨架：检查一部分高频错误，例如重复定义、未声明使用、`main` 入口、`break/continue` 位置、`const` 赋值、函数调用顺序等。
- 代码生成：当前已经支持一版朴素但可用的 RISC-V32 生成，能覆盖局部变量、赋值、算术/比较/逻辑表达式、`if/else`、`while`、`break/continue`、`return` 和基础函数调用。

还没完成的核心内容：

- 更完整的全局变量初始化
- 更系统的调用约定与寄存器管理
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

回归测试：

```bash
make test
```

这套测试现在会：

- 先检查能否成功编译或正确报错
- 再用仓库内置的轻量级 RISC-V32 解释执行器验证部分样例的实际返回值

当前回归样例覆盖了：

- 基础表达式和局部变量
- 注释和一元逻辑非
- 负整数字面量（含 `-2147483648`）
- `if/else`、`while`、`break`、`continue`
- 递归
- 作用域遮蔽
- 全局常量依赖链
- 多个全局变量的混合读取
- 全局声明与函数定义交错、且带运行期初始化
- 9 个参数的函数调用
- 嵌套函数调用
- 比较表达式与逻辑表达式优先级
- 空语句
- 全局变量写入
- 带负数的除法与取模
- `void` 函数作为语句调用
- 运行时全局初始化
- 一批典型语义报错

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

当前比较适合拿来回归的样例：

```bash
./build/toyc --input samples/basic.tc
./build/toyc --input samples/call.tc
./build/toyc --input samples/control.tc
```

目前 `-opt` 只做参数兼容，还没有真正启用优化。

## 建议你下一步优先补的顺序

1. 先把 `semantic.cpp` 里还没覆盖到的语义规则补全。
2. 再把 `codegen.cpp` 从“函数空壳”替换成真正的表达式和语句生成。
3. 最后再考虑 IR、优化和寄存器分配。
