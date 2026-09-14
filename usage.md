# dLua Usage Guide

Detailed usage instructions and command reference for **dLua**, a dynamic GDB-like Lua debugger on Linux.

[中文使用说明 (Chinese)](usage_CN.md)

---

## 1. Overview & Architecture

dLua attaches to an already running Lua process using dynamic library injection (`hookso`). It allows developers to set breakpoints, inspect variables, view stack traces, and modify execution state on the fly with zero modifications to source code.

```text
+------------------+         +--------------------+
|  Target Process  | <====== |    dlua (CLI)      |
| (running Lua VM) | IPC Msg | (interact via GDB- |
|  +dluaagent.so   |  Queue  |  style commands)   |
+------------------+         +--------------------+
```

---

## 2. Getting Started

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

## 3. Command Reference

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

---

## 4. Detailed Usage Examples

### 4.1 Flexible Breakpoint Syntax
dLua supports flexible spacing between filenames and line numbers:
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

### 4.2 Conditional Breakpoints
Variables needed for condition calculation must be declared in square brackets `[...]` to be retrieved from local/upvalue scopes:
```text
(dlua) b string_time_to_unix_time_with_tz if [tz] tz == 800
(dlua) b test.lua:44 if [time_sec, tz] time_sec > 0 and tz ~= nil
```

### 4.3 Call Stack & Frame Inspection
```text
(dlua) bt
0 in string_time_to_unix_time at test.lua:23
1 in ? at test.lua:59
2 in ? at [C]:-1

(dlua) f 1
#1 ? at test.lua:59
59 local begin_time1 = string_time_to_unix_time("2020-11-02 00:00:00")
```

### 4.4 Variable Inspection & Recursive Table Printing
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

### 4.5 Modifying Variables & Executing Code
```text
(dlua) set year = 2026
(dlua) set [tmp] tmp[1] = 99
(dlua) r print("debugging PID:", os.getenv("PID"))
```
