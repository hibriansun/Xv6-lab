#include "types.h"
#include "param.h"
#include "riscv.h"
#include "memlayout.h"
#include "fcntl.h"
#include "mmap.h"
#include "defs.h"
#include "fcntl.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "proc.h"

extern int
argfd(int n, int *pfd, struct file **pf);

// on success, it returns the address.
// on error, it returns 0xffffffffffffffff.
uint64
sys_mmap(void) {
    // args check
    int length, prot, flags, fd;
    struct file *file;
    argint(1, &length);
    argint(2, &prot);
    argint(3, &flags);
    if (length < 0 || length%PGSIZE != 0 || prot < PROT_NONE || flags < MAP_SHARED || argfd(4, &fd, &file) < 0) {
        goto err;
    }

    // The mmap accessibility should fit in the file-opened accessibility 
    if (file->writable == 0 && (prot & PROT_WRITE) && flags == MAP_SHARED) {
        goto err;
    }

    // find a unused vma to mmap
    struct proc *p = myproc();
    struct vm_area_struct *curr = 0;
    for (int i = 0; i < NVMA; i++) {
        if (p->vma_set[i].occupied == 0) {
            curr = &p->vma_set[i];
            break;
        }
    }
    if (curr == 0) {
        goto err;
    }
    // printf("%p %p\n", curr->vm_start, p->next_free_vmaddr);
    filedup(file);
    curr->vm_start = PGROUNDDOWN(p->next_free_vmaddr - length);
    curr->vm_length = length;
    p->next_free_vmaddr = curr->vm_start;
    curr->occupied = 1;
    curr->vm_file = file;
    curr->vm_file_offset = 0;
    curr->vm_flags = flags;
    curr->vm_page_prot = prot;

    // printf("%p %p\n", curr->vm_start, p->next_free_vmaddr);
    
    // printf("Mmap Done\n");

    return curr->vm_start;

err:
    return 0xffffffffffffffff;
}

// Assumepution:
// An munmap call might cover only a portion of an mmap-ed region, but you can
// assume that it will either unmap at the start, or at the end, or the whole region 
// (but not punch a hole in the middle of a region).
// {start, mid} or {mid, end} or {start, end}

// on success, it returns 0.
// on error, it returns -1.
uint64
sys_munmap(void) {
    struct proc *p = myproc();

    // args check
    uint64 addr; 
    int length;
    argaddr(0, &addr);
    argint(1, &length);
    if (addr >= TRAPFRAME || addr < p->next_free_vmaddr || length % PGSIZE != 0) {
        goto err;
    }

    for (int i = 0; i < NVMA; i++) {
        if (p->vma_set[i].occupied && (p->vma_set[i].vm_start+p->vma_set[i].vm_length == addr+length
         || p->vma_set[i].vm_start == addr)) {
            // unmap physical addresses mapping
            // check modified pages page by page and write data back to the file when MAP_SHARED is set
            for (int pglen = 0; pglen < length; pglen += PGSIZE) {
                // if the page is mapped physical addresses
                uint64 kmem = 0;
                if ((kmem = walkaddr(p->pagetable, addr+pglen)) != 0) {
                    if (p->vma_set[i].vm_flags == MAP_SHARED) {
                        if (p->vma_set[i].vm_page_prot & PROT_WRITE) {
                            begin_op();
                            ilock(p->vma_set[i].vm_file->ip);
                            writei(p->vma_set[i].vm_file->ip, 0, PGROUNDDOWN(kmem), addr+pglen-p->vma_set[i].vm_start, PGSIZE);
                            iunlock(p->vma_set[i].vm_file->ip);
                            end_op();
                        }
                        // if (not single occupid)
                        //     uvmunmap(p->pagetable, PGROUNDDOWN(addr+pglen), 1, 0);
                        // else
                        //     uvmunmap(p->pagetable, PGROUNDDOWN(addr+pglen), 1, 1);      // do free        
                    // } else {
                        uvmunmap(p->pagetable, PGROUNDDOWN(addr+pglen), 1, 1);      // do free
                    }
                }
            }
            
            // after unmapping, adjust vma mmapping
            if (p->vma_set[i].vm_start == addr) { 
                // unmmapped: {start, mid} or {start, end}
                p->vma_set[i].vm_length = p->vma_set[i].vm_length-length;
                p->vma_set[i].vm_start += length;
            } else if (p->vma_set[i].vm_start+p->vma_set[i].vm_length == addr+length) {
                // unmmapped: {mid, end}
                p->vma_set[i].vm_length = addr-p->vma_set[i].vm_start;
            }

            // free vma for unmmaping {start, end}
            if (p->vma_set[i].vm_length == 0) {
                fileundup(p->vma_set[i].vm_file);
                p->vma_set[i].occupied = 0;
            }

            return 0;
        }
    }

err:
    return -1;
}