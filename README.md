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
  - Execution Control: Next line (`n`), step into (`s`), finish function (`fin`), continue (`c`), until (`until`/`u`).
  - Inspection & Evaluation: Stack backtrace (`bt`), view/switch stack frames (`f`), inspect expressions (`p`), locals (`i locals`/`i l`), list source code (`l`).
  - Runtime Modification: Set local/upvalue/global variables (`set`), execute arbitrary Lua snippets (`r`).
  - Breakpoint Management: Enable (`en`), disable (`dis`), delete (`d`), temporary (`tbreak`/`tb`), inspect breakpoints (`i b`).

---

## Build & Installation

### Prerequisites
- Linux OS (e.g., Ubuntu / Debian / CentOS)
- GCC / G++ supporting C++11 or higher
- GNU Readline development library (`libreadline-dev`)
- [Lua 5.3+](https://www.lua.org/download.html) headers and library
- `gdb` (used to resolve Lua VM function addresses at attach time)
- [hookso](https://github.com/esrrhs/hookso) binary (not vendored; no git submodule — CI checks out a pinned SHA)
- ptrace permission: if `/proc/sys/kernel/yama/ptrace_scope` is `1`, either run as the process owner with scope relaxed to `0`, or ensure Yama allows the attach (otherwise hookso/gdb cannot attach)

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
4. Build or download [hookso](https://github.com/esrrhs/hookso) and make it discoverable (see **Runtime path lookup** below). Typical local layout: place `hookso` next to `dlua` / `dluaagent.so`.

### Runtime path lookup

`dlua` does **not** use a git submodule. It resolves tools at runtime:

**hookso** (`resolve_hookso` / `g_hookso`):
1. `$DLUA_HOOKSO` if executable
2. `dirname(/proc/self/exe)/hookso`
3. `./hookso`
4. `hookso` on `PATH`

Failure lists every location tried on stderr.

**dluaagent.so** (absolute path for `dlopen`):
1. `$DLUA_AGENT`
2. `dirname(exe)/dluaagent.so`
3. `./dluaagent.so`

After inject, hookso `call` still uses the soname `dluaagent.so`.

### Attach smoke (optional)

```bash
# With hookso beside the build products (or DLUA_HOOKSO / DLUA_AGENT set):
./scripts/smoke_attach.sh
```

This is a hookso-level inject check (no readline/expect): start `lua test.lua`, `argp`/`triggerp` dlopen → start/stop → dlclose, assert maps / call success.

---


## Limitations

- **Single Lua state per attach**: `dlua` attaches to one `lua_State*` discovered from the target process. Processes that create multiple Lua states are only debugged for the state that was found at attach time; switch/reattach is required to debug another state.
- **Linux + hookso + gdb**: Dynamic attach relies on [hookso](https://github.com/esrrhs/hookso) and `gdb` for address lookup; hookso is resolved at runtime (not packaged as a submodule).
- **Source path matching**: Breakpoints match Lua `short_src` with basename/suffix rules; prefer paths that appear in `bt` when ambiguous.

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
