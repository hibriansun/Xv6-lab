// init: The initial user-level program

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"
#include "kernel/fcntl.h"

char *argv[] = { "sh", 0 };

// 系统启动后运行的第一个'用户'进程
// 这个进程不允许退出，在exit()开始也会检查
int
main(void)
{
  int pid, wpid;

  if(open("console", O_RDWR) < 0){    // 打开 控制台 的文件描述符
    mknod("console", CONSOLE, 0);
    open("console", O_RDWR);
  }
  // 通过dup创建stdout和stderr。这里实际上通过复制文件描述符0，得到了另外两个文件描述符1，2。最终文件描述符0，1，2都用来代表Console
  // dup() system call creates a copy of the file descriptor oldfd
  // using the lowest-numbered unused file descriptor for the new descriptor.
  dup(0);  // stdout
  dup(0);  // stderr

  for(;;){
    printf("init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf("init: fork failed\n");
      exit(1);
    }
    if(pid == 0){
      exec("sh", argv);   // sh.c中从main开始执行
      printf("init: exec sh failed\n");
      exit(1);
    }

    for(;;){
      // this call to wait() returns if the shell exits,
      // or if a parentless process exits.
      wpid = wait((int *) 0);
      if(wpid == pid){
        // the shell exited; restart it.
        break;
      } else if(wpid < 0){
        printf("init: wait returned an error\n");
        exit(1);
      } else {
        // it was a parentless process; do nothing.
      }
    }
  }
}
