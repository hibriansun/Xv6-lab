# Context switch
#
#   void swtch(struct context *old, struct context *new);
# 
# Save current registers in old. Load from new.	

# 为什么RISC-V中有32个寄存器，但是swtch函数中只保存并恢复了14个寄存器？
# 因为swtch是按照一个普通函数来调用的，对于有些寄存器，swtch函数的调用者默认swtch函数会做修改，
# 所以调用者已经在自己的栈上保存了这些寄存器，当函数返回时，这些寄存器会自动恢复。所以swtch函数里只需要保存这些会被更改的Callee Saved Register就行。
# 而我们在swtch函数中只需要处理C编译器不会保存，但是对于swtch函数又有用且会占用的一些寄存器

# a0是调用swtch函数的第一个参数
# p->context(用户进程的内核线程使用的寄存器) store当前(尚未切换前)内核线程使用的寄存器内容到内核线程context数据结构做保存

# a1是调用swtch函数的第二个参数
# mycpu()->context是之前所属CPU的调度器线程使用的寄存器内容暂存的数据结构 这里将这些数据从内存中恢复load到寄存器中准备给调度器使用
# 这里关键是设置ra寄存器为上次离开调度器scheduler函数的地方 proc.c:488[swtch(&c->context, &p->context);]
.globl swtch
swtch:
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

        ld ra, 0(a1)
        ld sp, 8(a1)
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
        
        ret

	
