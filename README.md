# dLua

[<img src="https://img.shields.io/github/license/esrrhs/dLua">](https://github.com/esrrhs/dLua)
[<img src="https://img.shields.io/github/languages/top/esrrhs/dLua">](https://github.com/esrrhs/dLua)
[<img src="https://img.shields.io/github/actions/workflow/status/esrrhs/dLua/cmake.yml?branch=master">](https://github.com/esrrhs/dLua/actions)

A GDB-like dynamic debugger for Lua programs on Linux.

[中文文档 (Chinese)](README_CN.md)

---

## Features

- **Linux Platform Support**: Designed for Linux environments.
- **C++ Implementation**: High performance with minimal overhead.
- **Non-intrusive Dynamic Attach**: Attaches directly to already running Lua processes without code modification.
- **GDB-Style Debugging Interface**:
  - Breakpoints: Line breakpoints, function breakpoints, and conditional breakpoints. Flexible syntax supporting spaces (e.g., `b file.lua:10`, `b file.lua 10`, `b file.lua : 10`).
  - Execution Control: Next line (`n`), step into (`s`), finish function (`fin`), continue (`c`).
  - Inspection & Evaluation: Stack backtrace (`bt`), view/switch stack frames (`f`), inspect expressions (`p`), print tables, list source code (`l`).
  - Runtime Modification: Set local/upvalue/global variables (`set`), execute arbitrary Lua snippets (`r`).
  - Breakpoint Management: Enable (`en`), disable (`dis`), delete (`d`), inspect breakpoints (`i b`).

---

## Build & Installation

### Prerequisites
- Linux OS (e.g., Ubuntu / Debian / CentOS)
- GCC / G++ supporting C++11 or higher
- GNU Readline development library (`libreadline-dev`)
- [Lua 5.3+](https://www.lua.org/download.html) headers and library

### Build Steps
1. Clone the repository:
   ```bash
   git clone https://github.com/esrrhs/dLua.git
   cd dLua
   ```
2. Run `build.sh` to compile `dlua` (frontend CLI), `dluaagent.so` (debugger agent), and `test_unit`:
   ```bash
   ./build.sh
   ```
3. Run unit tests to verify:
   ```bash
   ./test_unit
   ```
4. Download or compile [hookso](https://github.com/esrrhs/hookso) (shared library injection utility), and place the `hookso` binary in the same directory as `dlua` and `dluaagent.so`.

---

## Quick Start

1. Start your target Lua process or use the sample script:
   ```bash
   lua test.lua
   ```
   Note the process PID (assume PID is `1234`).

2. Launch `dlua` and attach to the target PID:
   ```bash
   ./dlua 1234
   ```
   When the connection is established, you will see:
   ```text
   attach to 1234 ok, use ctrl+c to input command, eg: h
   ```

3. Press `Ctrl+C` to enter the prompt, then type `h` and press Enter to view help:
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

4. Press `Ctrl+C` anytime to interrupt execution and input commands:
   ```text
   (dlua) bt
   0 in string_time_to_unix_time at test.lua:23
   1 in ? at test.lua:50
   2 in ? at [C]:-1
   ```

5. Type `q` to detach and quit:
   ```text
   (dlua) q
   ```

---

## Command Reference

### `h`
Display help information.

### `q`
Detach from process and exit debugger.

### `bt`
Print current call stack backtrace.

### `b`
Set a breakpoint. Flexible syntax supports spaces:
- Break at file and line:
  ```text
  b test.lua:34
  b test.lua 34
  b test.lua : 34
  ```
- Break at line in current file:
  ```text
  b 34
  ```
- Break at current execution line:
  ```text
  b
  ```
- Break at function entry:
  ```text
  b string_time_to_unix_time_with_tz
  ```
- Break at nested table function entry:
  ```text
  b _G.test.getweekstart_by_tz_test
  ```
- Conditional breakpoint (variables inside `[...]` are extracted from the scope and passed into expression evaluation):
  ```text
  b string_time_to_unix_time_with_tz if [tz] tz==800
  b test.lua 34 if [a,b] a + b == 10
  ```

### `i`
Show debug information:
- List breakpoints:
  ```text
  i b
  ```

### `n`
Step over to next line.

### `s`
Step into function.

### `c`
Continue program execution.

### `dis`
Disable breakpoint:
- Disable specific breakpoint:
  ```text
  dis 1
  ```
- Disable all breakpoints:
  ```text
  dis
  ```

### `en`
Enable breakpoint:
- Enable specific breakpoint:
  ```text
  en 1
  ```
- Enable all breakpoints:
  ```text
  en
  ```

### `d`
Delete breakpoint:
- Delete specific breakpoint:
  ```text
  d 1
  ```
- Delete all breakpoints:
  ```text
  d
  ```

### `p`
Evaluate and print variable/expression:
- Print local/upvalue/global variable:
  ```text
  p year
  ```
- Print global table:
  ```text
  p _G.test
  ```
- Print expression using variables:
  ```text
  p [tmp] tmp.abc
  ```

### `l`
List source code around current line:
- Default view:
  ```text
  l
  ```
- View around line with radius:
  ```text
  l 20
  ```

### `f`
Select stack frame by index (from `bt` output):
```text
f 0
```

### `fin`
Finish execution of the current function call.

### `set`
Modify variable value:
- Set simple variable:
  ```text
  set year=1234
  ```
- Set global variable:
  ```text
  set _G.test = 123
  ```
- Set table property:
  ```text
  set [tmp] tmp.abc = 1
  ```

### `r`
Execute arbitrary Lua code:
- Execute statement:
  ```text
  r print(123)
  ```
- Execute with scoped variables:
  ```text
  r [tmp] tmp[1]=2
  ```

---

## Related Projects
- [lua-family-bucket](https://github.com/esrrhs/lua-family-bucket)
- [hookso](https://github.com/esrrhs/hookso)
