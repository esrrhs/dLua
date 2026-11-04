# dLua

[<img src="https://img.shields.io/github/license/esrrhs/dLua">](https://github.com/esrrhs/dLua)
[<img src="https://img.shields.io/github/languages/top/esrrhs/dLua">](https://github.com/esrrhs/dLua)
[<img src="https://img.shields.io/github/actions/workflow/status/esrrhs/dLua/cmake.yml?branch=master">](https://github.com/esrrhs/dLua/actions)

类似 GDB 的 Linux 平台 Lua 动态调试器。

[English Documentation](README.md) | [详细使用说明 (Usage Guide)](usage_CN.md)

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
- `gdb`（附加时用于解析 Lua VM 函数地址）
- [hookso](https://github.com/esrrhs/hookso) 可执行文件（不以内嵌 submodule 形式打包；CI 以固定 SHA checkout）
- ptrace 权限：若 `/proc/sys/kernel/yama/ptrace_scope` 为 `1`，需放宽为 `0` 或以允许 ptrace 的方式运行（否则 hookso/gdb 无法附加）

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
4. 编译或下载 [hookso](https://github.com/esrrhs/hookso)，并按下方 **运行时路径查找** 使其可被发现（常见做法：与 `dlua` / `dluaagent.so` 放在同级目录）。

### 运行时路径查找

`dlua` **不使用** git submodule，而是在运行时解析依赖：

**hookso**（`resolve_hookso` / `g_hookso`）：
1. `$DLUA_HOOKSO`（若可执行）
2. `dirname(/proc/self/exe)/hookso`
3. `./hookso`
4. `PATH` 上的 `hookso`

失败时会在 stderr 列出所有查找路径。

**dluaagent.so**（供 `dlopen` 的绝对路径）：
1. `$DLUA_AGENT`
2. `dirname(exe)/dluaagent.so`
3. `./dluaagent.so`

注入后，hookso `call` 仍使用 soname `dluaagent.so`。

### 附加冒烟测试（可选）

```bash
./scripts/smoke_attach.sh
```

基于 hookso 层注入（不使用 readline/expect）：启动 `lua test.lua`，执行 `argp`/`triggerp` dlopen → start/stop → dlclose，并断言 maps / call 成功。

---

## 快速使用

1. 启动待调试的目标 Lua 进程：
   ```bash
   lua test.lua
   ```
   记录对应进程的 PID（假设 pid 为 `1234`）。

2. 运行 `dlua` 附加到目标进程：
   ```bash
   ./dlua 1234
   ```
   连接建立后将显示如下提示：
   ```text
   attach to 1234 ok, use ctrl+c to input command, eg: h
   ```

3. 按 `Ctrl+C` 进入命令交互界面，输入调试指令，例如：
   ```text
   (dlua) b test.lua 23
   (dlua) bt
   (dlua) p year
   (dlua) c
   (dlua) q
   ```

> 详细的调试命令列表、参数格式与更多使用示例，请查阅 **[usage.md](usage.md)**。

---

## 相关项目
- [lua全家桶](https://github.com/esrrhs/lua-family-bucket)
- [hookso](https://github.com/esrrhs/hookso)

## 限制

- **每次附加只调试一个 Lua State**：`dlua` 会附加到目标进程中发现的一个 `lua_State*`。若进程内有多个 Lua 状态，仅能调试附加时所选中的那一个；调试其他状态需要重新附加。
- **仅支持 Linux + hookso + gdb**：动态附加依赖 [hookso](https://github.com/esrrhs/hookso) 与 `gdb` 地址查找；hookso 在运行时解析（非 submodule 打包）。
- **源路径匹配**：断点通过 basename/后缀规则匹配 Lua `short_src`；路径歧义时请优先使用 `bt` 中显示的路径。

