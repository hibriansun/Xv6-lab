	# qemu -kernel loads the kernel at 0x80000000
        # and causes each CPU to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
.section .text
_entry:
	# set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + (hartid * 4096)
        # 为每个CPU设置stack，例如供每个CPU上的调度内核线程使用(这个栈供以后CPU调度代码使用)
        la sp, stack0           # la: load absolute address -- sp = stack0
        li a0, 1024*4           # li: load immediate number -- a0 = 1024*4
	csrr a1, mhartid        # csrr: csr read -- a1 = mhartid
        addi a1, a1, 1          # addi: a1 = a1 + 1
        mul a0, a0, a1          # mul:  a0 = a0 * a1
        add sp, sp, a0          # add:  sp = sp + a0
	# jump to start() in start.c
        call start
spin:
        j spin
