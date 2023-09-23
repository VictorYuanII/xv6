#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 带参数列表，执行某个程序
void run(char *program, char **args) {
	if(fork() == 0) 
    { // 必须在子进程中调用
		exec(program, args);//路径-参数
		exit(0);
	}//一旦调用了 exec，当前进程的代码和数据都会被新程序替代，原有的程序逻辑将失效。
	return; // parent return
}

int main(int argc, char *argv[]){
	char buf[2048]; // 读入时使用的内存池,足够大的缓冲区
	char *p = buf, *last_p = buf; // 当前参数的结束、开始指针
	char *argsbuf[128]; // 全部参数列表，字符串指针数组，包含 argv 传进来的参数和 stdin 读入的参数
	char **args = argsbuf; // 指向指针的指针
	for(int i=1;i<argc;i++) 
	{//程序启动时传递的参数。
		*args = argv[i];
		args++;
	}
	char **pa = args; // 后面开始逐行读命令
	while(read(0, p, 1) != 0) {//标准输入stdin，循环到EOF
		if(*p == ' ' || *p == '\n') 
		{//  `echo zxf ptx`，则 zxf 和 ptx 各为一个参数
			*p = '\0';	//分割
			*(pa++) = last_p;//表这儿文法出现的字符串指针
			last_p = p+1;
			if(*p == '\n') 
			{// 读入一行完成
				*pa = 0; // 参数列表末尾用 null 标识列表结束
				run(argv[1], argsbuf); // 执行最后一行
				pa = args; // 重置读入参数指针，准备读入下一行
			}
		}
		p++;
	}
	if(pa != args) 
	{ // 最后一行非空
		*p = '\0';
		*(pa++) = last_p;// 收尾最后一行
		*pa = 0; // 参数列表末尾用 null 标识列表结束
		run(argv[1], argsbuf);// 执行最后一行指令
	}
	while(wait(0) != -1) {}; // 循环等待所有子进程完成，每一次 wait(0) 等待一个
    //如果调用进程没有子进程，wait(0) 会失败，并返回 -1
	exit(0);
}
