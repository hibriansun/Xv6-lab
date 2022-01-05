	.text

	/*
	 * save the old thread's registers,
	 * restore the new thread's registers.
	 */

// a0是调用thread_switch的第一个参数在的寄存器，a1是第二个参数在的寄存器

	.globl thread_switch
thread_switch:
	// save the callee-save registers
	sd ra, 0(a0)
	sd sp, 8(a0)
	sd s0, 16(a0)
	sd s1, 24(a0)
	sd s2, 32(a0)
	sd s3, 40(a0)
	sd s4, 48(a0)
	sd s5, 56(a0)
	sd s6, 64(a0)
	sd s7, 72(a0)
	sd s8, 80(a0)
	sd s9, 88(a0)
	sd s10, 96(a0)
	sd s11, 104(a0)

	// restore the callee-save registers
	ld ra, 0(a1)      # swtch结束后执行的指令在这里被设置，当swtch return(结束)，它将返回到ra中的指令(新线程之前调用swtch的地方)
	ld sp, 8(a1)      # CPU即将使用另一线程的栈空间
	ld s0, 16(a1)
	ld s1, 24(a1)
	ld s2, 32(a1)
	ld s3, 40(a1)
	ld s4, 48(a1)
	ld s5, 56(a1)
	ld s6, 64(a1)
	ld s7, 72(a1)
	ld s8, 80(a1)
	ld s9, 88(a1)
	ld s10, 96(a1)
	ld s11, 104(a1)

	ret    /* return to ra */
