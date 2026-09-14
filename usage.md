# dLua Usage Guide / 使用说明

Detailed usage instructions and command reference for **dLua**, a dynamic GDB-like Lua debugger on Linux.

[English](#english) | [中文说明](#中文说明)

---

<a name="english"></a>
## English

### 1. Overview & Workflow

dLua attaches to an already running Lua process using dynamic library injection (`hookso`). It allows developers to set breakpoints, inspect variables, inspect stack traces, and modify execution state on the fly.

```text
+------------------+         +--------------------+
|  Target Process  | <====== |    dlua (CLI)      |
| (running Lua VM) | IPC Msg | (interact via GDB- |
|  +dluaagent.so   |  Queue  |  style commands)   |
+------------------+         +--------------------+
```

### 2. Getting Started

1. **Launch your target Lua script**:
   ```bash
   lua test.lua
   ```
   Assume the process PID is `1234`.

2. **Attach using dlua**:
   ```bash
   ./dlua 1234
   ```
   Once attached, you will see:
   ```text
   attach to 1234 ok, use ctrl+c to input command, eg: h
   ```

3. **Interact with the debugger**:
   - Press `Ctrl+C` to interrupt the target and enter the `(dlua)` interactive prompt.
   - Enter commands (e.g. `h`, `bt`, `b`, `p`, etc.).
   - Press `Ctrl+C` whenever you want to issue new commands while running.

4. **Detach and exit**:
   ```text
   (dlua) q
   ```

---

### 3. Command Reference

| Command | Arguments | Description | Example |
|---|---|---|---|
| `h` | None | Display help message and list of supported commands | `h` |
| `q` | None | Detach from target process and exit | `q` |
| `bt` | None | Show current call stack backtrace | `bt` |
| `b` | `<file:line>` / `<file line>` / `<line>` / `<func>` `[if ...]` | Add a line, function, or conditional breakpoint | `b test.lua:34`<br>`b test.lua 34`<br>`b 34`<br>`b my_func`<br>`b test.lua:34 if [a,b] a+b==10` |
| `i b` | None | List current breakpoints and hit counts | `i b` |
| `n` | None | Step over to next line | `n` |
| `s` | None | Step into next line / function call | `s` |
| `c` | None | Continue program execution | `c` |
| `fin` | None | Finish execution of current function call | `fin` |
| `dis` | `[id]` | Disable specific breakpoint (or all if omitted) | `dis 1`<br>`dis` |
| `en` | `[id]` | Enable specific breakpoint (or all if omitted) | `en 1`<br>`en` |
| `d` | `[id]` | Delete specific breakpoint (or all if omitted) | `d 1`<br>`d` |
| `p` | `[expr]` / `[vars] expr` | Print expression / variable value | `p year`<br>`p _G.test`<br>`p [tmp] tmp.abc` |
| `l` | `[count]` | List source code around current line | `l`<br>`l 20` |
| `f` | `<frame_id>` | Select stack frame index (from `bt`) | `f 0`<br>`f 1` |
| `set` | `var = val` / `[vars] expr = val` | Set variable value | `set year=1234`<br>`set _G.test=1`<br>`set [tmp] tmp.a=2` |
| `r` | `<code>` / `[vars] <code>` | Run arbitrary Lua code snippet | `r print("hello")`<br>`r [tmp] tmp[1]=2` |

#### Detailed Command Examples

- **Flexible Breakpoint Syntax**:
  ```text
  (dlua) b test.lua:23
  Breakpoint 0 at file test.lua, line 23, if 
  code: local tmp = { 1, 2, { 4, 5 } }

  (dlua) b test.lua 23
  Breakpoint 1 at file test.lua, line 23, if 
  code: local tmp = { 1, 2, { 4, 5 } }

  (dlua) b test.lua : 23
  Breakpoint 2 at file test.lua, line 23, if 
  code: local tmp = { 1, 2, { 4, 5 } }
  ```

- **Conditional Breakpoint**:
  In conditional breakpoints, enclose variable names from the current scope inside `[...]` so they can be extracted:
  ```text
  (dlua) b string_time_to_unix_time_with_tz if [tz] tz == 800
  (dlua) b test.lua:44 if [time_sec, tz] time_sec > 0 and tz ~= nil
  ```

- **Call Stack & Frame Inspection**:
  ```text
  (dlua) bt
  0 in string_time_to_unix_time at test.lua:23
  1 in ? at test.lua:59
  2 in ? at [C]:-1

  (dlua) f 1
  #1 ? at test.lua:59
  59 local begin_time1 = string_time_to_unix_time("2020-11-02 00:00:00")
  ```

- **Variable Inspection & Table Printing**:
  ```text
  (dlua) p year
  "2020"

  (dlua) p _G.test
  [table: 0x55d7b5ca8150]
    1: 'test1'
    2: 'test2'
    3: 'test3'
    4: 'a\tb\tc'
  ```

---

<a name="中文说明"></a>
## 中文说明

### 1. 原理与流程

dLua 通过动态注入工具（`hookso`）将调试插件 `dluaagent.so` 注入到正在运行的 Lua 进程中，并通过 IPC 消息队列建立调试交互，支持无侵入式动态调试已上线的进程。

```text
+------------------+         +--------------------+
|   目标业务进程   | <====== |    dlua 控制台     |
| (已运行的 Lua VM)| 消息队列| (通过类 GDB 指令   |
|  +dluaagent.so   |   IPC   |  进行交互式调试)   |
+------------------+         +--------------------+
```

### 2. 快速使用步骤

1. **运行目标 Lua 程序**：
   ```bash
   lua test.lua
   ```
   记录对应进程的 PID（例如 `1234`）。

2. **使用 dlua 附加到进程**：
   ```bash
   ./dlua 1234
   ```
   附加成功后提示：
   ```text
   attach to 1234 ok, use ctrl+c to input command, eg: h
   ```

3. **进入调试交互**：
   - 随时按 `Ctrl+C` 中断程序并唤起 `(dlua)` 命令行提示符。
   - 输入调试指令（如 `h`, `bt`, `b`, `p`, `n`, `s`, `c` 等）。

4. **退出调试**：
   ```text
   (dlua) q
   ```

---

### 3. 命令详细说明

| 命令 | 参数格式 | 功能描述 | 示例 |
|---|---|---|---|
| `h` | 无 | 显示命令帮助与支持列表 | `h` |
| `q` | 无 | 退出调试并从目标进程分离 | `q` |
| `bt` | 无 | 显示当前调用堆栈 | `bt` |
| `b` | `<文件:行号>` / `<文件 行号>` / `<行号>` / `<函数>` `[if ...]` | 添加文件行断点、函数断点或条件断点 | `b test.lua:34`<br>`b test.lua 34`<br>`b 34`<br>`b my_func`<br>`b test.lua:34 if [a,b] a+b==10` |
| `i b` | 无 | 罗列当前断点列表及命中统计 | `i b` |
| `n` | 无 | 单步跳过（步过当前行） | `n` |
| `s` | 无 | 单步步入（进入函数内部） | `s` |
| `c` | 无 | 继续运行程序 | `c` |
| `fin` | 无 | 执行直到跳出当前函数 | `fin` |
| `dis` | `[编号]` | 禁用指定断点（省略则禁用所有断点） | `dis 1`<br>`dis` |
| `en` | `[编号]` | 启用指定断点（省略则启用所有断点） | `en 1`<br>`en` |
| `d` | `[编号]` | 删除指定断点（省略则删除所有断点） | `d 1`<br>`d` |
| `p` | `[表达式]` / `[变量表] 表达式` | 计算并打印变量或表达式 | `p year`<br>`p _G.test`<br>`p [tmp] tmp.abc` |
| `l` | `[半径行数]` | 查看当前行周围的源码 | `l`<br>`l 20` |
| `f` | `<帧编号>` | 选择栈帧（编号从 `bt` 查看） | `f 0`<br>`f 1` |
| `set` | `变量 = 值` / `[变量表] 表达式 = 值` | 修改局部/Upvalue/全局变量值 | `set year=1234`<br>`set _G.test=1`<br>`set [tmp] tmp.a=2` |
| `r` | `<代码>` / `[变量表] 代码` | 实时执行任意 Lua 代码片段 | `r print("hello")`<br>`r [tmp] tmp[1]=2` |

#### 典型使用场景示例

- **人性化断点空格语法**：
  文件和行号之间支持冒号或空格，无需担心空格误触：
  ```text
  b test.lua:34
  b test.lua 34
  b test.lua : 34
  ```

- **设置条件断点**：
  方括号内声明需要从当前作用域读取并参与计算的变量名：
  ```text
  b string_time_to_unix_time_with_tz if [tz] tz==800
  b test.lua 43 if [time_sec] time_sec > 1000000
  ```

- **查看调用栈与栈帧切换**：
  ```text
  (dlua) bt
  0 in string_time_to_unix_time at test.lua:23
  1 in ? at test.lua:59
  2 in ? at [C]:-1

  (dlua) f 1
  #1 ? at test.lua:59
  59 local begin_time1 = string_time_to_unix_time("2020-11-02 00:00:00")
  ```

- **变量查看与表结构递归格式化**：
  ```text
  (dlua) p year
  "2020"

  (dlua) p _G.test
  [table: 0x55d7b5ca8150]
    1: 'test1'
    2: 'test2'
    3: 'test3'
    4: 'a\tb\tc'
  ```
