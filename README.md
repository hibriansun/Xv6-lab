## Xv6 编译启动
![Xv6启动.png](https://s4.ax1x.com/2022/03/03/bYbin0.png)

## Xv6 entry -- entry.S
![bYxErj.png](https://s4.ax1x.com/2022/03/03/bYxErj.png)

> 为了接下来的内核C函数可以执行，要为每个CPU设置函数执行的内核> 栈，每个核心的内核栈大小为4096bytes (A page in xv6)


## Xv6 start()
![bYxOYV.png](https://s4.ax1x.com/2022/03/03/bYxOYV.png)

> 每个CPU都要设置一下，使每个CPU都能接收到Trap并在Supervisor Mode能陷入
>
> Trap: 
>    1. 中断
>        来自中断控制器的External Interrupt
>        来自软件发出的Software Interrupt
>    2. Exception
>        除零操作、Page Fault、Syscall etc.
>
> 接着每个CPU进入main.c:main
>

## Xv6 main()
![btpRF1.png](https://s4.ax1x.com/2022/03/03/btpRF1.png)

> 仅有hart id为0的CPU核心会进入第一个用户进程`user/init.c` (userinit的行为是exec(user/_init))
> 所有的核心都会完成许多内核模块的初始化任务(见代码注释)
> 
> 最终，除了第一个运行init进程的CPU，剩下CPU被调度器分配调度任务
> 

## Xv6 init.c -- main()
![btCp36.png](https://s4.ax1x.com/2022/03/03/btCp36.png)

> 父进程通过fork子进程保证有shell可以执行，其在一个infinity loop中等待子进程shell退出 
> 一旦退出重新建立一个shell进程，这样我们就可以通过子进程进入到了shell，用其执行其他程序了
> 
------------------------------------------------------------------------

xv6 is a re-implementation of Dennis Ritchie's and Ken Thompson's Unix
Version 6 (v6).  xv6 loosely follows the structure and style of v6,
but is implemented for a modern RISC-V multiprocessor using ANSI C.

ACKNOWLEDGMENTS

xv6 is inspired by John Lions's Commentary on UNIX 6th Edition (Peer
to Peer Communications; ISBN: 1-57398-013-7; 1st edition (June 14,
2000)). See also https://pdos.csail.mit.edu/6.828/, which
provides pointers to on-line resources for v6.

The following people have made contributions: Russ Cox (context switching,
locking), Cliff Frey (MP), Xiao Yu (MP), Nickolai Zeldovich, and Austin
Clements.

We are also grateful for the bug reports and patches contributed by
Silas Boyd-Wickizer, Anton Burtsev, Dan Cross, Cody Cutler, Mike CAT,
Tej Chajed, Asami Doi, eyalz800, , Nelson Elhage, Saar Ettinger, Alice
Ferrazzi, Nathaniel Filardo, Peter Froehlich, Yakir Goaron,Shivam
Handa, Bryan Henry, jaichenhengjie, Jim Huang, Alexander Kapshuk,
Anders Kaseorg, kehao95, Wolfgang Keller, Jonathan Kimmitt, Eddie
Kohler, Austin Liew, Imbar Marinescu, Yandong Mao, Matan Shabtay,
Hitoshi Mitake, Carmi Merimovich, Mark Morrissey, mtasm, Joel Nider,
Greg Price, Ayan Shafqat, Eldar Sehayek, Yongming Shen, Fumiya
Shigemitsu, Takahiro, Cam Tenny, tyfkda, Rafael Ubal, Warren Toomey,
Stephen Tu, Pablo Ventura, Xi Wang, Keiichi Watanabe, Nicolas
Wolovick, wxdao, Grant Wu, Jindong Zhang, Icenowy Zheng, and Zou Chang
Wei.

The code in the files that constitute xv6 is
Copyright 2006-2020 Frans Kaashoek, Robert Morris, and Russ Cox.

ERROR REPORTS

Please send errors and suggestions to Frans Kaashoek and Robert Morris
(kaashoek,rtm@mit.edu). The main purpose of xv6 is as a teaching
operating system for MIT's 6.S081, so we are more interested in
simplifications and clarifications than new features.

BUILDING AND RUNNING XV6

You will need a RISC-V "newlib" tool chain from
https://github.com/riscv/riscv-gnu-toolchain, and qemu compiled for
riscv64-softmmu. Once they are installed, and in your shell
search path, you can run "make qemu".
