// open system call's second params
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200     // If pathname does not exist, create it as a regular file. Nor do nothing.
#define O_TRUNC   0x400     // If  the file already exists and is a regular file and the access mode allows writing (i.e., is O_RDWR or  O_WRONLY) 
                            // it will be truncated(截断) to length 0.  If the file is a FIFO or terminal device file, the O_TRUNC flag is ignored.
                            // Otherwise, the effect of O_TRUNC is unspecified.
