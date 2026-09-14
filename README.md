# dLua

[<img src="https://img.shields.io/github/license/esrrhs/dLua">](https://github.com/esrrhs/dLua)
[<img src="https://img.shields.io/github/languages/top/esrrhs/dLua">](https://github.com/esrrhs/dLua)
[<img src="https://img.shields.io/github/actions/workflow/status/esrrhs/dLua/cmake.yml?branch=master">](https://github.com/esrrhs/dLua/actions)

A GDB-like dynamic debugger for Lua programs on Linux.

[中文文档 (Chinese)](README_CN.md) | [Detailed Usage Guide](usage.md)

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

1. Start your target Lua process:
   ```bash
   lua test.lua
   ```
   Assume PID is `1234`.

2. Launch `dlua` and attach to the target PID:
   ```bash
   ./dlua 1234
   ```
   When connected:
   ```text
   attach to 1234 ok, use ctrl+c to input command, eg: h
   ```

3. Press `Ctrl+C` to interrupt execution and enter commands. For example:
   ```text
   (dlua) b test.lua 23
   (dlua) bt
   (dlua) p year
   (dlua) c
   (dlua) q
   ```

> For full command references, detailed options, and examples, please see **[usage.md](usage.md)**.

---

## Related Projects
- [lua-family-bucket](https://github.com/esrrhs/lua-family-bucket)
- [hookso](https://github.com/esrrhs/hookso)
