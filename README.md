# LetherScript v1.2

> 一个轻量级的基于C++开发的 Windows 控制台脚本语言解释器，支持变量、数组、函数、字符串操作、数学运算、文件 I/O、彩色输出等功能。
>
> A lightweight Windows console script language developed based on C++ interpreter with variables, arrays, functions, string operations, math functions, file I/O, and colored console output.

---

## 快速开始 / Quick Start

### 编译 / Build

```bash
g++ -std=c++20 -o LetherScript-v1.2.exe LetherScript-v1.2.cpp
```

### 运行 / Run

```bash
# 运行脚本 / Run a script
.\LetherScript-v1.2.exe codes\calc.lts

# 交互模式 / Interactive REPL mode
.\LetherScript-v1.2.exe
```

---

## 示例脚本 / Example Scripts

所有示例在 `codes/` 目录下 / All examples are in the `codes/` directory:

| 文件 / File | 说明 / Description |
|---|---|
| `calc.lts` | 简单计算器 / Simple calculator |
| `test_features.lts` | 综合功能演示 / Feature demo |
| `test_fileio.lts` | 文件 I/O 测试 / File I/O demo |
| `test_using.lts` | 头文件引用 / Header inclusion |
| `test_minimal.lts` | 最小测试 / Minimal test |

头文件在 `headers/` 目录下 / Headers are in the `headers/` directory:
- `math_utils.lth` — 数学工具 / Math utilities
- `io_utils.lth` — I/O 工具 / I/O utilities
- `string_utils.lth` — 字符串工具 / String utilities

---

## 语法参考 / Language Reference

### 1. 变量定义 / Variable Definition

```lts
define x;              // int32 变量, 初始值 0
define y = 42;         // 带初始值 / With initial value
define s = "hello\n";  // 字符串 / String
define c = 'A';        // 字符 / Character
define i64 = 42i;      // 64位整数 / 64-bit integer
define a[5] = (0);     // 数组, 全 0 / Array, all zeros
define a[3] = (1,2,3); // 数组, 初始化 / Array, initialized
define filestream f;   // 文件流 / File stream
```

### 2. 赋值 / Assignment

```lts
x = 42;
s = "world";
a[1] = 99;
```

### 3. 输出 / Output

```lts
output("Hello\n");             // 字符串 / String
output(x);                     // 变量 / Variable
output("a = ", a, "\n");      // 多参数拼接 / Multi-arg concatenation
output(format("x = {x}\n"));  // 格式化字符串 / Format string
colorfulText("C", "red text"); // 彩色文本, 不换行 / Colored text, no newline
```

### 4. 输入 / Input

```lts
input(x, "Enter x: ");            // 读取标记 / Read token
inputLine(s, "Enter line: ");     // 读取一行 / Read line
```

### 5. 数学函数 / Math Functions

```lts
sqrt(25)    // → 5
abs(-42)    // → 42
ceil(3.2)   // → 4
floor(3.8)  // → 3
round(3.5)  // → 4
pow(2, 10)  // → 1024
sin(0)      // → 0
cos(0)      // → 1
log(1)      // → 0
log10(100)  // → 2
min(1, 5)   // → 1
max(1, 5)   // → 5
fmod(10, 3) // → 1
```

### 6. 字符串方法 / String Methods

```lts
s.size()             // 长度 / Length
s.empty()            // 是否为空 / Is empty
s.find("sub")        // 查找 / Find (returns -1 if not found)
s.rfind("sub")       // 反向查找 / Reverse find
s.substr(0, 5)       // 子串 / Substring (pos, len)
s.sub(0, 5)          // 子串 / Substring (start, end)
s.replace(1, 2, "x") // 替换 / Replace
s.append("!")        // 追加 / Append
s.insert(3, "xyz")   // 插入 / Insert
s.erase(2, 3)        // 删除 / Erase
s.compare("abc")     // 比较 / Compare (-1/0/1)
s.starts_with("A")   // 前缀检查 / Starts with
s.ends_with("Z")     // 后缀检查 / Ends with
```

### 7. 数组方法 / Array Methods

```lts
arr.push(42)    // 尾部推入 / Push to back
arr.delete(0)   // 删除下标 / Delete at index
```

### 8. 函数定义 / Function Definition

```lts
define add(a, b) :{
    return a + b;
}

define fact(n) :{
    if (n <= 1) :{
        return 1;
    }
    return n * fact(n - 1);
}

// 调用 / Call
x = add(3, 4);       // → 7
output(fact(5));     // → 120
```

### 9. 条件判断 / If Statement

```lts
if (x > 0) :{
    output("positive\n");
} else :{
    output("non-positive\n");
}
```

花括号可放在 `:{` 或单独一行 / Brace can follow `:{` or be on its own line.

### 10. 延时与退出 / Sleep & Quit

```lts
sleep(1000);  // 暂停 1 秒 / Sleep 1 second
quit();       // 立即退出 / Quit immediately
quit(2000);   // 2 秒后退出 / Quit after 2 seconds
```

### 11. 文件 I/O / File I/O

```lts
// 输出重定向 / Output redirection
output.file("out.txt");
output("This goes to file\n");
output.fileClose();

// 文件流 / File stream
define filestream f;
f.open("data.txt");
inputLine.fileStream(f, line);
output(format("Read: {line}\n"));
f.close();

output.fileStream(f, "written text\n");
input.fileStream(f, token);
```

### 12. 系统命令 / System Command

```lts
system("dir /b");
```

### 13. 头文件引用 / Header Inclusion

```lts
using "C:/path/header.lth";  // 绝对路径 / Absolute path
using "header.lth";           // 相对路径 / Relative to script
using <math_utils>;           // 从 headers/ 目录 / From headers/ folder
```

### 14. 注释 / Comments

```lts
// 单行注释 / Single-line comment
/* 多行注释 / Multi-line
   comment */
code /* 行内注释 */ more;
```

### 15. 全局指令 / Global Directives

```lts
$fullScreen on$     // 全屏模式 / Fullscreen mode
$traceLog on$       // 追踪日志 / Trace logging
$debugMessage on$    // 调试消息 / Debug messages
```

---

## 项目结构 / Project Structure

```
LetherScript/
├── LetherScript-v1.2.cpp      ← 解释器源代码 / Interpreter source
├── headers/                   ← 头文件库 / Header library
│   ├── math_utils.lth
│   ├── io_utils.lth
│   └── string_utils.lth
└── codes/                     ← 示例脚本 / Example scripts
    ├── calc.lts
    ├── test_features.lts
    ├── test_fileio.lts
    ├── test_using.lts
    └── test_minimal.lts
```

---

## 编译要求 / Build Requirements

- **编译器 / Compiler**: GCC (MinGW-W64) 11+ 或 MSVC
- **标准 / Standard**: C++20 (`-std=c++20`)
- **依赖 / Dependencies**: Windows SDK (用于控制台 API) / Windows SDK (for console API)
- **平台 / Platform**: Windows (使用了 Win32 API)

---

## 许可证 / License

MIT License
