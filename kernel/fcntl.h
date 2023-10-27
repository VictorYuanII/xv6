#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400
#define O_NOFOLLOW 0x800//用于指示打开文件时不要跟随符号链接。
//如果结果等于 0，表示不设置 O_NOFOLLOW，允许跟随符号链接。如果结果不等于 0，表示设置了 O_NOFOLLOW，不允许跟随符号链接，应该直接处理符号链接本身而不是跟随它。