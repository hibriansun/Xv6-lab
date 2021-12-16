#include "kernel/types.h"
#include "user/user.h"

int main()
{
    int pid;
    char c;
    printf("Multi-core and uni-core have different results.\n");

    pid = fork();
    if(pid == 0){
        c = '/';
    } else {
        c = '\\';
        printf("Child's pid is %d, parent's pid is %d\n", pid, getpid());
    }

    for (int i = 0; ; i++){
        if(i % 1000000 == 0){
            write(1, &c, 1);
        }
    }

    exit(0);
}