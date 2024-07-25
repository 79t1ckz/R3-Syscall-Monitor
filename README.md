# R3-Syscall-Monitor

在R3下追踪特定的函数call，不使用调试器框架，能够保证在大量插桩的情况下进程几乎原速运行。

Trace Random func call ( not just syscall ) in only usermode, without debugger framework, and keeps process in a normal speed.

# How to trace function what I want ?

使用txt文件描述模块的插桩地址。支持导出名称 \ 导出序号 \ 偏移。

create a txt file in the "/tasks" directory as a "task set", the basic grammar just like:

  &lt;module1&gt;

  // This is a comment
  
    "MessageBoxA"  "optional description"
    
    #1234       // <- Oridinal in hex
    
    +0x1234     // <- Rva from Image Base
    
  &lt;/module1&gt;
  

  &lt;module2&gt;
    
  ...
  
  &lt;/module2&gt;
  
use "hook" command to apply them.

Counter module and logger module show the call records. 

Counter can show call frequency and count. 

Logger can show caller's return-back chain and thread's id, it can also show the records one-by-one or stack them.

![](snapshots/1.png)
![](snapshots/2.png)
![](snapshots/3.png)
![](snapshots/4.png)
![](snapshots/5.png)

# What should I notice ?

此工具仅支持x86-64的windows平台。logger模块运行起来可能会有点不太靠谱，尤其是乱钩地址的情况下。如果你想追踪一个游戏，计数器模块更加稳定。

This tool is only for WINDOWS and X86-64, maybe support more in the future.

Logger module may be not very stable if you want to trace a RANDOM address. ( fxsave is not used, maybe used in the future )

If you want to trace a game, the counter module is better.

# Tool tips

如果你不知道该怎么钩，可以从系统调用下手。

If you don't know where to start, you can start to trace the syscall.

# future target

可能会出一个GUI版本，并添加参数、输出捕获功能。说实话，这种工具其实挺鸡肋的，一个是要过完整性检测，一个是游戏一般不需要那么深度的逆向（除非是要做什么高大上的G）。
