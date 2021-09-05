// xargs
// xargs可以将stdin中以空格或换行符进行分隔的数据，形成以空格分隔的参数（arguments），传递给其他命令。
// 因为以空格作为分隔符，所以有一些文件名或者其他意义的字符串内含有空格的时候，xargs可能会误判。
// 简单来说，xargs的作用是给其他命令传递参数，是构建单行命令的重要组件之一。

// 题目hints：从标准输入中读取管道前命令exec并将输出数据以空白为分界作为一个个参数传递给xargs作为标准输入
// i.e.
// find . -name 'file*' -print | xargs cat
// 输出当前目录(.)下所有以file开头文件/目录的文件路径，每一个路径间有一个\n作为分割空白将一个个路径传给cat(想要防止目录空格被误认为是分隔符，开启 -0 选项)

// 但本题有些许特殊 管道前命令执行后的输出作为本程序的标准输入读取即可
// 传入被程序的参数列表是管道后命令作为argv

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

int
main(int argc, char* argv[])
{
    // argv参数从index = 0开始为 "xargs"
    // for(int i = 0; i < argc; i++){
    //     printf("Args[%d]: %s\n", i, argv[i]);
    // }

    // Read data from STDIN
    int n = 0;
    char line[1024];
    char* param[MAXARG];        // for exec
    int param_index = 0;

    // 先行填充xargs后紧接参数
    for (int i = 1; i < argc; i++){ 
        param[param_index] = argv[i];
        param_index++;
    }

    while((n = read(STDIN_FILENO, line, 1024))){        // 从管道前的输出读; "find . b | xargs grep hello" 即读取find . b结果
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

            // for(int k = 0; k < param_index; k++){
            //     printf("\n== %s ==\n", param[k]);   
            // }

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