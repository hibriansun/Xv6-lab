/*
 * This struct defines a memory VMM memory area. There is one of these
 * per VM-area/task.  A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (i.e. mmap
 * area, the executable area etc).
 */
struct vm_area_struct {
    int occupied;           /* Whether the vma is used for a process */

    uint64 vm_start;
    int vm_length;

    int vm_page_prot;       /* Access permissions of this VMA. */
    int vm_flags;           /* Private or shared */

    struct file * vm_file;	/* File we map to */
    int vm_file_offset;
};

#define NVMA 16

