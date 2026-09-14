# dLua

[<img src="https://img.shields.io/github/license/esrrhs/dLua">](https://github.com/esrrhs/dLua)
[<img src="https://img.shields.io/github/languages/top/esrrhs/dLua">](https://github.com/esrrhs/dLua)
[<img src="https://img.shields.io/github/actions/workflow/status/esrrhs/dLua/cmake.yml?branch=master">](https://github.com/esrrhs/dLua/actions)

类似 GDB 的 Linux 平台 Lua 动态调试器。

[English Documentation](README.md)

---

## 特性

- **支持 Linux 平台**：针对 Linux 环境设计。
- **C++ 实现**：性能高效，内存开销低。
- **非侵入式动态附加**：通过附加到正在运行的 Lua 进程进行调试，无需修改原程序代码。
- **GDB 风格的交互调试**：
  - 断点功能：支持文件行断点、函数入口断点以及条件断点；语法宽容灵活，文件和行号之间允许空格（如 `b file.lua:10`、`b file.lua 10`、`b file.lua : 10`）。
  - 执行控制：单步跳过（`n`）、单步进入（`s`）、跳出函数（`fin`）、继续执行（`c`）。
  - 检查与查看：调用堆栈（`bt`）、切换栈帧（`f`）、打印变量与表达式（`p`）、格式化查看表数据、查看源码（`l`）。
  - 运行时修改：修改局部变量/Upvalue/全局变量（`set`）、实时执行任意 Lua 代码（`r`）。
  - 断点管理：启用（`en`）、禁用（`dis`）、删除（`d`）、查看断点列表（`i b`）。

---

## 编译与安装

### 前置要求
- Linux 操作系统（如 Ubuntu / Debian / CentOS 等）
- 支持 C++11 及以上标准的 GCC / G++
- GNU Readline 开发库（`libreadline-dev`）
- [Lua 5.3+](https://www.lua.org/download.html) 头文件与开发库

### 编译步骤
1. 克隆代码仓库：
   ```bash
   git clone https://github.com/esrrhs/dLua.git
   cd dLua
   ```
2. 执行 `build.sh` 脚本进行编译，将生成 `dlua`（控制台前端）、`dluaagent.so`（调试插件库）及 `test_unit`（单元测试）：
   ```bash
   ./build.sh
   ```
3. 运行单元测试验证：
   ```bash
   ./test_unit
   ```
4. 下载编译 [hookso](https://github.com/esrrhs/hookso)（共享库动态注入工具），将编译得到的 `hookso` 与 `dlua`、`dluaagent.so` 放置在同级目录下即可使用。

---

## 快速使用

1. 启动待调试的目标 Lua 进程，也可以运行项目自带的示例脚本：
   ```bash
   lua test.lua
   ```
   记录进程 PID（假设 pid 为 `1234`）。

2. 运行 `dlua` 附加到目标进程：
   ```bash
   ./dlua 1234
   ```
   连接建立后将显示如下提示：
   ```text
   attach to 1234 ok, use ctrl+c to input command, eg: h
   ```

3. 按 `Ctrl+C` 进入命令交互界面，输入 `h` 回车查看帮助：
   ```text
   (dlua) h
   h       help commands
   q       quit
   bt      show cur call stack
   b       add breakpoint, eg: b test.lua:123
   i       show info, eg: i b
   n       step next line
   s       step into next line
   c       continue run
   dis     disable breakpoint, eg: dis 1
   en      enable breakpoint, eg: en 1
   d       delete breakpoint, eg: d 1
   p       print exp value, eg: p _G.xxx
   l       list code
   f       select stack frame
   fin     finish current call
   set     set value, eg: set aa=1
   r       run code, eg: r print("test")
   ```

4. 调试过程中随时可通过 `Ctrl+C` 中断执行并输入调试指令：
   ```text
   (dlua) bt
   0 in string_time_to_unix_time at test.lua:23
   1 in ? at test.lua:50
   2 in ? at [C]:-1
   ```

5. 输入 `q` 退出调试器并断开连接：
   ```text
   (dlua) q
   ```

---

## 命令参考

### `h`
显示命令帮助信息。

### `q`
退出调试器并从目标进程分离。

### `bt`
查看当前调用堆栈。

### `b`
设置断点。支持灵活的空格语法：
- 设置在指定文件的指定行：
  ```text
  b test.lua:34
  b test.lua 34
  b test.lua : 34
  ```
- 设置在当前执行文件的指定行：
  ```text
  b 34
  ```
- 设置在当前正执行到的位置：
  ```text
  b
  ```
- 设置在函数入口：
  ```text
  b string_time_to_unix_time_with_tz
  ```
- 设置在嵌套表函数入口：
  ```text
  b _G.test.getweekstart_by_tz_test
  ```
- 条件断点（方括号 `[...]` 内指定需要从当前作用域读取并参与计算的变量名）：
  ```text
  b string_time_to_unix_time_with_tz if [tz] tz==800
  b test.lua 34 if [a,b] a + b == 10
  ```

### `i`
查看调试信息：
- 查看断点列表：
  ```text
  i b
  ```

### `n`
执行下一行（步过，函数不进入）。

### `s`
执行下一行（步入函数内部）。

### `c`
继续执行程序。

### `dis`
禁用断点：
- 禁用指定序号断点：
  ```text
  dis 1
  ```
- 禁用所有断点：
  ```text
  dis
  ```

### `en`
启用断点：
- 启用指定序号断点：
  ```text
  en 1
  ```
- 启用所有断点：
  ```text
  en
  ```

### `d`
删除断点：
- 删除指定序号断点：
  ```text
  d 1
  ```
- 删除所有断点：
  ```text
  d
  ```

### `p`
计算并打印变量或表达式值：
- 打印局部/Upvalue/全局变量：
  ```text
  p year
  ```
- 打印全局表对象：
  ```text
  p _G.test
  ```
- 打印复杂属性（方括号指定引入变量）：
  ```text
  p [tmp] tmp.abc
  ```

### `l`
查看当前执行位置附近的源码：
- 默认行数：
  ```text
  l
  ```
- 指定半径行数：
  ```text
  l 20
  ```

### `f`
选择栈帧编号（具体编号从 `bt` 查看）：
```text
f 0
```

### `fin`
跳过执行当前函数至返回。

### `set`
修改变量值：
- 修改普通变量：
  ```text
  set year=1234
  ```
- 修改全局变量：
  ```text
  set _G.test = 123
  ```
- 修改表成员属性：
  ```text
  set [tmp] tmp.abc = 1
  ```

### `r`
实时执行 Lua 代码：
- 执行语句：
  ```text
  r print(123)
  ```
- 带变量参数执行：
  ```text
  r [tmp] tmp[1]=2
  ```

---

## 相关项目
- [lua全家桶](https://github.com/esrrhs/lua-family-bucket)
- [hookso](https://github.com/esrrhs/hookso)
