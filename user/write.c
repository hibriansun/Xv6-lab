#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

void main()
{
    int fd = open("TEST2", O_CREATE | O_RDWR);
    char buf[((10-1-1-2) / 2) * BSIZE + 10] = {0};
    for (int i = 0; i < ((10-1-1-2) / 2) * BSIZE + 9; i++) {
        buf[i] = 'x';
    }
    printf("%s\n", buf);
    write(fd, buf, ((10-1-1-2) / 2) * BSIZE + 10);

    printf("\n    Xv6 can't guarantee the write syscall is atomic.\n");
    printf("\n\
    For the POSIX standard: When POSIX says that write() is atomic,\n\
    it does not mean that the call has no effect when it fails,\n\
    and it certainly is not talking about behavior in the event of a system crash.\n\
    It is talking about atomicity with respect to system calls by other threads.\n\
    The same fd can't be written concurrently.\n");
    printf("Something useful: https://www.notthewizard.com/2014/06/17/are-files-appends-really-atomic/\n");
}