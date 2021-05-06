// 题目要求：从标准输入中读取管道前命令输出数据
// 使用exec调用(Linux中完全版应使用execvp)
// i.e.
// char* arr[] = {"", "Hello", "World", "SZ", 0};
// execvp("echo", arr);         // 对arr中每个参数使用echo命令

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// void myExec(char* args1[], char* args2[]){
//     // pipefd for communicating with two processes
//     int pipefd[2];
//     pipe(pipefd);

//     pid_t pid = fork();

//     // redirection
//     switch (pid)
//     {
//     case 0:
//     // Child Wirte things to write end of pipe
//     // The  dup()  system call creates a copy of the file descriptor
//     // oldfd, using the lowest-numbered unused file  descriptor  for
//     // the new descriptor.

//         close(STDOUT_FILENO);
//         dup(pipefd[1]);     // redirection write end    file discreptor 1 redirect to pipefd[1] (4) poniting file
//         close(pipefd[1]);   // only file discreptor 1 point to pipe write end

//         exec(args1[0], args1);

//         write(STDERR_FILENO, "Exec Fail\n", sizeof("Exec Fail\n"));
//         break;
//     case -1:
//         write(STDERR_FILENO, "Fork Error\n", sizeof("Fork Error\n"));
//         exit(-1);

//     default:
//         wait(0);
//         close(pipefd[1]);
//         close(STDIN_FILENO);
//         dup(pipefd[0]);

//         exec(args2[0], args2);

//         write(STDERR_FILENO, "Exec Fail\n", sizeof("Exec Fail\n"));
//         break;
//     }

// }

int
main(int argc, char* argv[])
{
    // for(int i = 0; i < argc; i++){
    //     printf("Args[%d]: %s\n", i, argv[i]);
    // }

    // ========= Additional Implement ========
    // Implementation of pipe operator
    // like: "who | ls"
    // ======================================

    // Spilt commands
    // if(argc > MAXARG || argc < 1){
    //     printf("Invalid args\n");
    //     exit(1);
    // }
    // char* args1[MAXARG], *args2[MAXARG];
    // int flag = -1;
    // for(int i = 0; i < argc; ++i){
    //     if(strcmp("|", argv[i]) == 0){
    //         flag = i;
    //         continue;
    //     }
    //     if(flag == -1){
    //         args1[i] = argv[i];
    //     }else{
    //         args2[i - flag - 1] = argv[i];
    //     }
    // }

    // for(int i = 0; i < flag; ++i){
    //     printf("%s\n", args1[i]);
    // }

    // for(int i = 0; i < argc - flag; ++i){
    //     printf("%s\n", args2[i]);
    // }

    // myExec(args1, args2);


    // Formal Implementation
    
    // Read data from STDIN
    int n = 0;
    char line[1024];
    char* param[MAXARG];        // for exec
    int param_index = 0;

    for (int i = 1; i < argc; i++){ 
        param[param_index] = argv[i];
        param_index++;
    }

    while((n = read(STDIN_FILENO, line, 1024))){
        pid_t pid = fork();
        if(pid == 0){
            char *tmp;
            int tmp_index = 0;

            // Split parameters
            for(int i = -1; i < n; i++){        // index of `line` char arr
                // It's time to spilt :)
                if(i == -1 || line[i] == ' ' || line[i] == '\n'){
                    if(i != -1){
                        tmp[tmp_index] = 0;
                        param[param_index] = tmp;
                        param_index++;
                        tmp_index = 0;
                    }

                    tmp = (char*)malloc(sizeof(line));      // Brand new space for a new string
                }else{      // Append
                    tmp[tmp_index] = line[i];
                    tmp_index++;
                }
            }
            param[param_index] = 0; // Final

            exec(argv[1], param);     // argv[1] is the command that is behind pipe operator
            
        }else if(pid == -1){
            write(STDERR_FILENO, "Fork Fail\n", sizeof("Fork Fail\n"));
            exit(1);
        }else{
            wait(0);
        }
    }

    exit(0);
}