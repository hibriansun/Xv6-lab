#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void find(char* path, char* name);

void
listInfo(char* path){
    printf("%s\n", path);      // TODO
}

char*
getFileName(char* path){
    char *s = path;
    for(int i = 0; i < strlen(path); i++){
        if(path[i] == '/'){
            s = &path[i] + 1;
        }
    }
    return s;
}

char*
getDirName(char* path){
    char *s = path, *f = path;
    for(int i = 0; i < strlen(path); i++){
        if(path[i] == '/'){
            s = f;
            f = &path[i];
            if(path[i+1] == 0){
                return s+1;
            }
        }
    }
    return s;
}

// 对文件：检查是否匹配
// 对目录：看目录名是否匹配 -- 输出目录下所有文件级目录下目录
//                不匹配 -- 递归下去find
void
check(char* path, char* name){
    int fd;
    struct stat st;
    // printf("%s\n\n", path);

    if((fd = open(path, 0)) < 0){
        printf("Debug: %d\n", fd);
        fprintf(2, "C find: cannot open %s\n", path);
        exit(1);
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }
    
    char* fileName, *dirName;
    switch (st.type)
    {
    case T_FILE:
    case T_DEVICE:
        fileName = getFileName(path);
        if(strcmp(name, fileName) == 0){
            char buf[512];
            strcpy(buf, path);
            printf("%s\n", buf);
        }
        close(fd);
        // printf("%s Close successfully.\n", path);
        break;
    
    case T_DIR:
        dirName = getDirName(path);
        if(strcmp(name, dirName) == 0){
            listInfo(path);
            close(fd);
        }else{
            // 遍历目录下所有文件
            find(path, name);
        }
        break;

    default:
        fprintf(2, "Assertion Error\n");
        break;
    }
}

// 对某一目录path下所有文件遍历检查
void
find(char* path, char* name){
    // 对当前目录每一个文件
    struct dirent de;
    int fd;

    if((fd = open(path, 0)) < 0){
        fprintf(2, "F find: open %s error", path);
        exit(1);
    }
    // int i = 0;

    while(read(fd, &de, sizeof(de)) == sizeof(de)){
        // printf("%d\n", i++);
        // printf("Debug: %s %d\n", de.name, de.inum);
        
        if(de.inum == 0){
            break;
        }
        if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0){
            continue;
        }
        char buf[512];
        strcpy(buf, path);
        strcat(buf, "/");
        strcat(buf, de.name);

        check(buf, name);
    }

    close(fd);
}


int
main(int argc, char* argv[])
{
    if(argc < 3){
        fprintf(2, "find: Usage: find <dir> <fileName>\n");
        exit(1);
    }

    find(argv[1], argv[2]);

    exit(0);
}
