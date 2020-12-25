# dLua

[<img src="https://img.shields.io/github/license/esrrhs/dLua">](https://github.com/esrrhs/dLua)
[<img src="https://img.shields.io/github/languages/top/esrrhs/dLua">](https://github.com/esrrhs/dLua)
[<img src="https://img.shields.io/github/workflow/status/esrrhs/dLua/CI">](https://github.com/esrrhs/dLua/actions)

类似gdb的lua调试器

# 特性
* 支持Linux平台
* C++编写
* 通过附加到其他进程上，进行调试
* gdb风格的调试指令，包括设置条件断点、查看变量、设置变量

# 编译
* 下载编译安装[lua](https://www.lua.org/download.html)
* 用脚本编译dlua，生成```dlua```与```dluaagent.so```，```dlua```是控制台，```dluaagent.so```是调试插件
```
# ./build.sh
```
* 下载编译[hookso](https://github.com/esrrhs/hookso)，生成```hookso```，```hookso```是注入工具
* 最后将```dlua```、```dluaagent.so```、```hookso```放在同级目录即可使用

# 使用
* 找到目标进程pid，也可以使用项目中的示例代码。假设pid=1234
```
# lua test.lua
```

* 运行dlua，附加到1234进程，出现如下提示，说明连接正常，可以开始调试
```
# ./dlua 1234
attack to 1234 ok, use ctrl+c to input command, eg: h
```

* 输入ctrl+c，输入h回车查看帮助
```
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

* 其他命令同理，输入ctrl+c，输入命令即可
```
(dlua) bt
0 in string_time_to_unix_time at test.lua:23
1 in ? at test.lua:50
2 in ? at [C]:-1
```

* 退出，则输入q
```
(dlua) q
#
```

# 命令
### h
帮助
### q
退出
### bt
查看调用堆栈
### b
打断点，打在某个文件某一行
```
b test.lua:34
```
打在当前文件的某一行
```
b 34
```
打在当前正执行到的行
```
b 
```
打在某个函数的入口
```
b string_time_to_unix_time_with_tz
```
打在某个嵌套函数的入口
```
b _G.test.getweekstart_by_tz_test
```
条件断点，方括号的tz表示需要的参数，作为输入参与到后面的表达式计算
```
b string_time_to_unix_time_with_tz if [tz] tz==800
```
### i
罗列当前的断点
```
i b
``` 
### n
下一行，如果当前位置是函数，则跳过内部
### s
下一行，如果当前位置是函数，则跳进内部
### c
取消步进，继续执行
### dis
取消断点，取消某个断点
```
dis 1
```
取消所有断点
```
dis
```
### en
生效断点，生效某个断点
```
en 1
```
生效所有断点
```
en
```
### d
删除断点，删除某个断点
```
d 1
```
删除所有断点
```
d
```
### p
查看当前栈的变量，如
```
p year
```
或者全局的
```
p _G.test
```
或者复杂的，查看table中的某一项，[]表示需要传入的变量，作为输入参与到后面的表达式计算
```
p [tmp] tmp.abc
```
### l
查看当前栈的附近代码，如
```
l
```
查看附近20行的代码
```
l 20
```
### f
设置当前栈帧，具体编号从bt查看，如
```
f 0
```
### fin
跳过执行当前函数
### set
设置当前栈的变量，如
```
set year=1234
```
或者全局的
```
set _G.test = 123
```
或者复杂的，设置table中的某一项，[]表示需要传入的变量，作为输入参与到后面的表达式计算
```
set [tmp] tmp.abc = 1
```
### r
运行特定代码
```
r print(123)
```
运行复杂代码，加上变量，[]表示需要传入的变量，作为输入参与到后面的表达式计算
```
r [tmp] tmp[1]=2
```

## 其他
[lua全家桶](https://github.com/esrrhs/lua-family-bucket)
