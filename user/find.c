#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path, char *target) {
	char buf[512], *p;//完整路径长度，经验选择512
	int fd;//用于标识打开文件或其他 I/O 设备的句柄,以便后续的文件操作可以使用它来引用打开的文件或设备。
	struct dirent de;//表示目录中的条目（文件和子目录）
	struct stat st;
	if((fd = open(path, 0)) < 0)
    {//打不开
		printf("find: cannot open %s\n", path);
		return;
	}
	if(fstat(fd, &st) < 0)//权限检查、文件大小计算、时间戳的管理等
    {//无法获取状态信息
		printf("find: cannot stat %s\n", path);
		close(fd);
		return;
	}

	switch(st.type){//类型-文件/文件夹 
	case T_FILE:// 文件名后缀匹配
		if(strcmp(path+strlen(path)-strlen(target), target) == 0) 
			printf("%s\n", path);
		break;
	case T_DIR://文件夹 
		if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){//超过512，寄
			printf("find: path too long\n");
			break;
		}
		strcpy(buf, path);//buf=path 
		p = buf+strlen(buf);//指向buf后
		*p++ = '/';//放入/
		while(read(fd, &de, sizeof(de)) == sizeof(de))//sizeof(de)=2+14
        {//从目录文件fd中读取一个目录项，并存在de中。每次调用read会读取下一个目录项，确保了每次迭代都处理一层路径。
			if(de.inum == 0) continue;//该目录项无效
			memmove(p, de.name, DIRSIZ);//memcpy 14
			p[DIRSIZ] = 0;//确保字符串以 null 结尾
			// Don't recurse into "." and "..".
			if(strcmp(buf+strlen(buf)-2, "/.") != 0 && strcmp(buf+strlen(buf)-3, "/..") != 0) 
				find(buf, target); // 递归
		}
		break;
	}
	close(fd);
}

int main(int argc, char *argv[])
{
	if(argc < 3) exit(0);
	char target[512];//要查找的目标文件
	target[0] = '/'; // 默认根目录下运行
	strcpy(target+1, argv[2]);//要查找的目标文件的名称
	find(argv[1], target);//argv[1]要搜索的起始目录的路径
	exit(0);
}
