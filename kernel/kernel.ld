/* 链接脚本 */
/* 用于说明内核这么多编译后的.o文件如何被链接起来成为一个ELF可执行文件 */

OUTPUT_ARCH( "riscv" )
ENTRY( _entry )       /* 这个可执行文件的入口在symbol _entry 这里(entry.S) */

SECTIONS
{
  /*
   * ensure that entry.S / _entry is at 0x80000000,
   * where qemu's -kernel jumps.
   */
  /* 将整个内核可执行文件load到qemu创建的分区中的0x80000000上 */
  /* "."表示当前位置是 */
  . = 0x80000000;

  /* 可执行文件中的.text段安排 */
  .text : {
    *(.text .text.*)    /* 所有目标文件的.text段全部放到这个内核可执行文件的.text段 */
    . = ALIGN(0x1000);  /* 这个.text段大小做一个页对其(A page in xv6 is 4096 bytes.) */
    _trampoline = .;    /* 用一个变量_trampoline记录下当前位置 */
    *(trampsec)         /* 将所有目标文件中的trampsec部分 */
    . = ALIGN(0x1000);  /* 做一个4K对齐 */
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");  /* 当前位置与_trampoline位置做比较不能超过一个页大小 */
    PROVIDE(etext = .); /* 记录当前位置到etext 并且PROVIDE将etext作为一个指针变量供予后续C程序使用 */
  }

  /* Read Only Data 段 */
  /* e.g. 字符串、const常量 */
  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  /* Data 段 */
  /* 初始化了的：全局静态变量 局部静态变量 */
  /* Initialized static variables */
  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
  }

  /* BSS 段 */
  /* 在硬盘文件未加载到内存中时 BSS段不占用硬盘空间 */
  /* 该段保证所有变量为0 */
  /* 未初始化的：全局普通变量、全局静态变量、局部静态变量 (或者说是所有初始化为0值的变量) */
  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*) /* do not need to distinguish this from .bss */
    . = ALIGN(16);
    *(.bss .bss.*)
  }

  /* 记录内核可执行程序的结束地址 并赋值给end 再将end作为一个C变量提供给后续的内核程序(kalloc.c) */
  PROVIDE(end = .);
}
