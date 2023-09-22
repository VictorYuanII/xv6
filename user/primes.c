#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 一次 dfs 筛一个素数
void dfs(int L[2]) { 
	int p;
	read(L[0], &p, sizeof(p)); // 素
	if(p == -1) exit(0); // flag
	printf("prime %d\n", p);
	int R[2]; pipe(R);
	if(fork()==0) 
    {// 子进程 
		close(R[1]); // 操作系统对每个进程有文件描述符数量的限制，如果不关闭不需要的文件描述符
		close(L[0]); // ，可能会导致文件描述符用尽的问题，进而导致程序异常。
		dfs(R); 
        exit(0);
	} else {// 父进程 
		close(R[0]); 
		int buf;
		while(read(L[0], &buf, sizeof(buf)) && buf != -1) 
			if(buf % p != 0) //重写
				write(R[1], &buf, sizeof(buf)); 
		buf = -1;
		write(R[1], &buf, sizeof(buf)); //-1
		wait(0); // 等待任何子进程终止,不能等待子进程的子进程
		exit(0);
	}
}

int main(int argc, char **argv) {
	int que[2];
	pipe(que);

	if(fork()==0) 
    {//子进程
		close(que[1]);  
		dfs(que);
		exit(0);//终止一个进程
	} else {
		close(que[0]); // 同上
		int i;
		for(i=2;i<=35;i++) write(que[1], &i, sizeof(i));//先进先出
		i=-1;
		write(que[1], &i, sizeof(i)); // 末尾输入 -1，用于标识输入完成
        wait(0);//放外面一样
	    exit(0);
	}
}

