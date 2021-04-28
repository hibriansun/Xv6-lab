/**
 * Mention:
 *      1. 从管道读数据时：如果写入端文件描述符全部被关闭，那么read buf直到遇到0后返回读出的字符数[这是我们期待的]
 *                      如果写入端文件描述符没有被全部关闭(引用计数 > 0)，read读取数据至管道内没有数据时会阻塞住
 *      2. 往管道中写数据时：如果读出端所有文件描述符全部被关闭，写入管道时进程会收到SIGPIPE信号导致进程异常中止(当然可以实施信号捕捉)
 *                        如果读出端所有文件描述符未被完全关闭，写入管道直至管道被写满时，write被阻塞[我们所期待的]
 * 
 * Thus:
 *      要从管道读数据前保证写入端无文件描述符(引用计数为0)
 *      要往管道里写数据保证读出端有文件描述符引用(引用计数不为0)
*/

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/stat.h"

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDDER_FILENO 2

#define NULL 0

#define READ_END 0
#define WRITE_END 1

int
main(int argc, char* argv[])
{
    // pipefd[0] refers to the read end of the pipe. pipefd[1] refers to the write end of the pipe
    int pipefdP2C[2];
    int pipefdC2P[2];
    pipe(pipefdP2C);
    pipe(pipefdC2P);
    
    pid_t cpid = fork();
    if(cpid == 0){
        // P --> C
        char byte[8];
        close(pipefdP2C[WRITE_END]);
        read(pipefdP2C[READ_END], byte, 8);
        close(pipefdP2C[READ_END]);     // Whatever
        printf("%d: received p%sng\n", getpid(), byte);
        
        
        // C --> P
        close(pipefdC2P[READ_END]);
        write(pipefdC2P[WRITE_END], "o", 1);
        close(pipefdC2P[WRITE_END]);
        exit(0);
    }else{
        char byte[8];
        // P --> C
        close(pipefdP2C[READ_END]);
        write(pipefdP2C[WRITE_END], "i", 1);
        close(pipefdP2C[WRITE_END]);

        // C --> P
        close(pipefdC2P[WRITE_END]);
        read(pipefdC2P[READ_END], byte, 8);
        close(pipefdC2P[READ_END]);      // Whatever
        printf("%d: received p%sng\n", getpid(), byte);
        exit(0);
    }
}
