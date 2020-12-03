#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <strings.h>
#include <sys/mman.h>

#define PAGE_SIZE 4096

int main()
{
	int clientfd, listenfd;
	struct sockaddr_un servaddr, cliaddr;
	int ret;
	struct msghdr msg;
	struct iovec iov[1];
	char buf[100];
	union {
		struct cmsghdr cm;
		char control[CMSG_SPACE(sizeof(int))];
	} control_un;
	struct cmsghdr *cmptr;
	int recvfd;
	socklen_t len;
	unsigned char *buffer;

	listenfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listenfd < 0) {
		printf("socket failed\n");
		return listenfd;
	}

	unlink("test_socket");
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sun_family = AF_UNIX;
	strcpy(servaddr.sun_path, "test_socket");
	//servaddr.sun_path = "test_socket";

	ret = bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
	if (ret < 0) {
		printf("bind socket failed.\n");
		close(listenfd);
		return ret;
	}

	listen(listenfd, 5);

	while(1) {
		len = sizeof(cliaddr);
		clientfd = accept(listenfd, (struct sockaddr *)&cliaddr, &len);
		if (clientfd < 0) {
			printf("accept failed\n");
			continue;
		}
		msg.msg_control = control_un.control;
		msg.msg_controllen = sizeof(control_un.control);

		msg.msg_name = NULL;
		msg.msg_namelen = 0;
		iov[0].iov_base = buf;
		iov[0].iov_len = sizeof(buf)/sizeof(buf[0]);

		msg.msg_iov = iov;
		msg.msg_iovlen = 1;

		ret = recvmsg(clientfd, &msg, 0);
		if (ret < 0) {
			return ret;
		}

		//check recv data and len
		if ((cmptr = CMSG_FIRSTHDR(&msg)) != NULL && (cmptr->cmsg_len == CMSG_LEN(sizeof(int)))) {
			if(cmptr->cmsg_level != SOL_SOCKET) {
				printf("cmsg_level is not SOL_SOCKET\n");
				continue;
			}

			if (cmptr->cmsg_type != SCM_RIGHTS) {
				printf("cmsg_type is not SCM_RIGHTS\n");
				continue;
			}

			recvfd = *((int *)CMSG_DATA(cmptr));
			break;
		}
	}
	printf("recv fd:%d\n", recvfd);
	buffer = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, recvfd, 0);
	if (buffer == MAP_FAILED) {
		close(recvfd);
		printf("map dmmbuf fail\n");
		return MAP_FAILED;
	}

	printf("service buffer data:%s\n", buffer);

	ret = munmap(buffer, PAGE_SIZE);
	if (ret < 0) {
		printf("munmap error\n!");
	}

	close(recvfd);

	return ret;
}
