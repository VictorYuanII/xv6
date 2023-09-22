#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h" 
//暂停指定的嘀嗒数（与OS有关）
int main(int argc, char **argv) {
	if(argc < 2) {
		printf("usage: sleep <ticks>\n");//错误信息
		exit(1);
	}
	sleep(atoi(argv[1]));//变整数
	exit(0);
}



