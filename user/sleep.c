#include "kernel/types.h"
#include "user/user.h"

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

int
main(int argc, char* argv[])
{
    if(argc != 2){
        write(STDERR_FILENO, "sleep: missing operand\n", strlen("sleep: missing operand\n"));
        write(STDERR_FILENO, "Usage: sleep seconds\n", strlen("Usage: sleep seconds\n"));
        exit(1);
    }
    
    uint ticks = atoi(argv[1]);
    if(sleep(ticks) != 0){
        write(STDERR_FILENO, "Sleep was interrupted by a signal handler.\n", strlen("Sleep was interrupted by a signal handler.\n"));
    }

    exit(0);
}
