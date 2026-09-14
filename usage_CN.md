# dLua 使用说明

Linux 平台 Lua 动态调试器 **dLua** 的详细使用说明与命令手册。

[English Usage Guide](usage.md)

---

## 1. 原理与架构

dLua 通过动态注入工具（`hookso`）将调试扩展动态库 `dluaagent.so` 注入到正在运行的目标 Lua 进程中，并通过 System V IPC 消息队列与前端控制台 `dlua` 建立双向通信，从而实现非侵入式动态在线调试。

```text
+------------------+         +--------------------+
|   目标业务进程   | <====== |    dlua 控制台     |
| (已运行的 Lua VM)| 消息队列| (通过类 GDB 指令   |
|  +dluaagent.so   |   IPC   |  进行交互式调试)   |
+------------------+         +--------------------+
```

---

## 2. 快速使用步骤

1. **运行待调试的 Lua 程序**：
   ```bash
   lua test.lua
   ```
   记录进程的 PID（假设为 `1234`）。

2. **使用 dlua 附加到目标进程**：
   ```bash
   ./dlua 1234
   ```
   附加成功后将输出：
   ```text
   attach to 1234 ok, use ctrl+c to input command, eg: h
   ```

3. **交互式调试**：
   - 随时按 `Ctrl+C` 中断程序运行，进入 `(dlua)` 命令行交互提示符。
   - 输入调试命令（如 `h`, `bt`, `b`, `p`, `n`, `s`, `c` 等）。
   - 执行中若需再次输入命令，直接按 `Ctrl+C` 即可随时唤起命令行。

4. **退出调试器**：
   ```text
   (dlua) q
   ```

---

## 3. 调试命令速查表

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

---

## 4. 典型使用示例

### 4.1 人性化断点空格语法
支持文件与行号之间使用冒号或空格分隔：
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

### 4.2 设置条件断点
方括号 `[...]` 内声明需要从当前栈帧/作用域提取的变量名，后续表达式可直接使用这些变量参与计算：
```text
(dlua) b string_time_to_unix_time_with_tz if [tz] tz == 800
(dlua) b test.lua 43 if [time_sec] time_sec > 1000000
```

### 4.3 查看调用堆栈与栈帧切换
```text
(dlua) bt
0 in string_time_to_unix_time at test.lua:23
1 in ? at test.lua:59
2 in ? at [C]:-1

(dlua) f 1
#1 ? at test.lua:59
59 local begin_time1 = string_time_to_unix_time("2020-11-02 00:00:00")
```

### 4.4 变量查看与递归格式化表结构
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

### 4.5 运行时修改变量与执行代码
```text
(dlua) set year = 2026
(dlua) set [tmp] tmp[1] = 99
(dlua) r print("debugging PID:", os.getenv("PID"))
```
