
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char **argv) {
	// 0读 1写	
	int ptc[2], ctp[2];
	pipe(ptc); // 父-> 子
	pipe(ctp); // 子-> 父
	
    int flag=fork();//创建子‘进’程
    if(flag<0) //失败
    {
        printf("fail\n");
        exit(1);
    }

	if(flag != 0) 
    { // 父进程
		write(ptc[1], "Y", 1); //写字符串Y的前1位
		char buf;
		read(ctp[0], &buf, 1); //buf=ctp[0] 读
		printf("%d: received pong\n", getpid()); // 成功
		wait(0);
	} else { //子进程
		char buf;
		read(ptc[0], &buf, 1); //读
		printf("%d: received ping\n", getpid());//打印进程id
		write(ctp[1], &buf, 1); // 写
	}
	exit(0);
}
