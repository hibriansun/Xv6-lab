// Your goal is to use `pipe` and `fork` to set up the pipeline. The first process feeds the numbers 2 through 35 
// into the pipeline. For each prime number, you will arrange to create one process that reads from its 
// left neighbor over a pipe and writes to its right neighbor over another pipe. 
// Since xv6 has limited number of file descriptors and processes, the first process can stop at 35.


// Theory shows here
// ![gFx0KJ.png](https://z3.ax1x.com/2021/04/29/gFx0KJ.png)

// 要得到自然数n以内的全部素数，必须把不大于√n的所有素数的倍数剔除，剩下的就是素数
// 列出2以后的所有序列：
// 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25
// 标出序列中的第一个素数，也就是2，序列变成：
// 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25
// 将剩下序列中，划掉2的倍数，序列变成：
// 2 3 5 7 9 11 13 15 17 19 21 23 25
// 如果这个序列中最大数小于最后一个标出的素数的平方，那么剩下的序列中所有的数都是素数，否则回到第二步。
// 本例中，因为25大于2的平方，我们返回第二步：
// 剩下的序列中第一个素数是3，将主序列中3的倍数划掉，主序列变成：
// 2 3 5 7 11 13 17 19 23 25
// 我们得到的素数有：2，3
// 25仍然大于3的平方，所以我们还要返回第二步：
// 序列中第一个素数是5，同样将序列中5的倍数划掉，主序列成了：
// 2 3 5 7 11 13 17 19 23
// 我们得到的素数有：2，3，5 。
// 因为23小于5的平方，跳出循环.
// 结论：2到25之间的素数是：2 3 5 7 11 13 17 19 23。

/*
大致逻辑：
    while(lastPrime * lastPrime <= n){
        pid = fork();
        switch(pid){
            case 0:
                readFromPipeline();
                judgeNumbers();
                writeResult();
                break;
            case -1:
                ERROR;
                exit(1);
            default:
                print(lastPrime);
                break;
        }
    }

*/

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define READ_END 0
#define WRITE_END 1

void Sieved(int parentPipe[], int n, int lastPrime){
    if(n < 2){
        exit(0);
    }
    if(lastPrime*lastPrime > n){
        close(parentPipe[WRITE_END]);       // READ_END will get EOF
        int tmp;
        while(read(parentPipe[READ_END], &tmp, sizeof(int)) == sizeof(int)){
            printf("prime %d\n", tmp);
        }
        close(parentPipe[READ_END]);
        return; 
    }

    printf("prime %d\n", lastPrime);

    pid_t pid = fork();

    // 不要在fork前创建childpipe，否则在子进程哪里还有一对文件描述符连接着管道的读和写，造成子进程出现阻塞
    int childPipe[2];
    pipe(childPipe);

    if(pid == 0){
        close(parentPipe[WRITE_END]);

        int tmp, flag = 0, nextLastPrime = lastPrime;

        while(read(parentPipe[READ_END], &tmp, sizeof(int)) == sizeof(int)){
            if(tmp % lastPrime != 0){
                if(flag == 0){
                    nextLastPrime = tmp;
                    flag = 1;
                }
                // printf("%d -- WRITE\n", tmp);
                write(childPipe[WRITE_END], &tmp, sizeof(int));
            }
        }
        close(parentPipe[READ_END]);        // parentPipe使命到此结束，两端所有文件描述符消失，管道资源被释放
        // printf(" Before Recursion\n");
        // printf("Next %d\n", nextLastPrime);
        Sieved(childPipe, n, nextLastPrime);
    }else if (pid == -1){
        fprintf(2, "fork error\n");
        exit(1);
    }else{
        close(parentPipe[READ_END]);
        close(parentPipe[WRITE_END]);       // Child will see EOF
        wait((int*)0);
        exit(0);
    }
}

int main()
{
    int n = 35;
    
    // init Pipeline
    int initPipe[2];
    pipe(initPipe);

    for(int i = 2; i <= n; i++){
        write(initPipe[WRITE_END], &i, sizeof(int));
    }

    // 在fork后再在父进程中关闭READ_END/WRITE_END，否则会子进程fork后READ_END/WRITE_END文件描述符是没有的

    Sieved(initPipe, n, 2);

    exit(0);
}
