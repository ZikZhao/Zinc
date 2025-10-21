# PolyType - CMake Build System

这个项目已经从Makefile迁移到CMake构建系统，并完全集成到VS Code中。

## 使用方法

### 调试运行
- 直接按 `F5` 或者在VS Code中选择 "Debug Interpreter" 配置开始调试
- 程序会自动使用CMake构建，然后启动调试器

### 手动构建
- 按 `Ctrl+Shift+P` 打开命令面板
- 选择 "Tasks: Run Task"
- 选择 "CMake Build" 来构建项目

### 清理构建
- 按 `Ctrl+Shift+P` 打开命令面板
- 选择 "Tasks: Run Task" 
- 选择 "CMake Clean" 来清理构建目录

## 构建输出
- 可执行文件位置：`build/out/interpreter`
- 生成的解析器文件：`build/out/parser.tab.cpp`, `build/out/parser.tab.hpp`
- 生成的词法分析器文件：`build/out/lex.yy.cc`

## 扩展推荐
确保安装以下VS Code扩展：
- C/C++ (ms-vscode.cpptools)
- CMake Tools (ms-vscode.cmake-tools)

这些扩展已经在工作区配置中设为推荐安装。