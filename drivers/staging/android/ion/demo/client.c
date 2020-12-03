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
#include "dmabuf_test.h"
#include "ion.h"

#define PAGE_SIZE 4096

static int test_share_dmabuf_from_kernel(int fd)
{
	int ret = 0;
	unsigned char *buf = NULL;
	struct dmabuf_test_rw_data data = {0};

	ret = ioctl(fd, DMABUF_IOC_TEST_GET_FD_FROM_KERNEL, &data);
	if (ret < 0) {
		printf("get dmabuf fd faile\n");
		return ret;
	}

	buf = (unsigned char *)mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, data.fd, 0);
	if (buf == MAP_FAILED) {
		close(data.fd);
		printf("map dmabuf fail\n");
		return -1;
	}
	printf("user space buffer data:%s\n", buf);
	ret = munmap(buf, PAGE_SIZE);
	if (ret < 0) {
		printf("munmap error\n!");
	}

	close(data.fd);
	return 0;
}

static int test_share_dmabuf_to_kernel(int fd)
{
	int ret = 0;
	struct dmabuf_test_rw_data data = {0};
	int ion_fd = 0;
	struct ion_allocation_data alloc_data;
	struct ion_handle_data handle_data;
	struct ion_fd_data fd_data;
	unsigned char *buffer;

	ion_fd = open("/dev/ion", O_RDONLY);
	if (ion_fd < 0) {
		printf("open ion device faile\n");
		return ion_fd;
	}

	alloc_data.len = PAGE_SIZE;
	alloc_data.align = 0;
	alloc_data.heap_id_mask = ION_HEAP_SYSTEM_MASK;
	alloc_data.flags = ION_FLAG_CACHED | ION_FLAG_CACHED_NEEDS_SYNC;

	/* alloc dmabuf */
	ret = ioctl(ion_fd, ION_IOC_ALLOC, &alloc_data);
	if (ret < 0) {
		printf("ion alloc error\n");
		goto close_fd;
	}

	/* get dmabuf fd */
	fd_data.handle = alloc_data.handle;
	ret = ioctl(ion_fd, ION_IOC_MAP, &fd_data);
	if (ret < 0) {
		printf("ion map error\n");
		goto free_buf;
	}

	//mmap to user space
	buffer = mmap(NULL, alloc_data.len, PROT_READ|PROT_WRITE, MAP_SHARED,
			fd_data.fd, 0);
	if (MAP_FAILED == buffer) {
		printf("mmap to user space error!\n");
		goto close_dmabuf_fd;
	}

	buffer[0] = 'a';
	buffer[1] = 'b';
	buffer[2] = 'c';
	buffer[3] = 'd';
	buffer[4] = '\n';

	data.fd = fd_data.fd;

	/* share dmabuf fd to kernel */
	ret = ioctl(fd, DMABUF_IOC_TEST_GET_FD_FROM_USER, &data);
	if (ret < 0) {
		printf("share dmabuf fd to kernel space error\n");
	}

	ret = munmap(buffer, alloc_data.len);
	if (ret < 0) {
		printf("munmap error\n!");
	}

close_dmabuf_fd:
	close(fd_data.fd);

free_buf:
	handle_data.handle = alloc_data.handle;
	ret = ioctl(ion_fd, ION_IOC_FREE, &handle_data);
	if (ret)
		printf("ion free buffer error\n");
close_fd:
	close(ion_fd);
	return ret;
}

int test_share_dmabuf_in_process()
{
	int ret;
	int ion_fd = 0;
	struct ion_allocation_data alloc_data;
	struct ion_handle_data handle_data;
	struct ion_fd_data fd_data;
	unsigned char *buffer;
	/* use to process communication */
	int socket_fd;
	struct sockaddr_un addr;
	struct msghdr msg;
	union {
		struct cmsghdr cm;
		char control[CMSG_SPACE(sizeof(int))];
	} control_un;
	struct cmsghdr *cmptr;
	struct iovec iov[1];
	char buf[100];			//for what?

	ion_fd = open("/dev/ion", O_RDONLY);
	if (ion_fd < 0) {
		printf("open ion device faile\n");
		return ion_fd;
	}

	alloc_data.len = PAGE_SIZE;
	alloc_data.align = 0;
	alloc_data.heap_id_mask = ION_HEAP_SYSTEM_MASK;
	alloc_data.flags = ION_FLAG_CACHED | ION_FLAG_CACHED_NEEDS_SYNC;

	/* alloc dmabuf */
	ret = ioctl(ion_fd, ION_IOC_ALLOC, &alloc_data);
	if (ret < 0) {
		printf("ion alloc error\n");
		goto close_fd;
	}

	/* get dmabuf share fd */
	fd_data.handle = alloc_data.handle;
	ret = ioctl(ion_fd, ION_IOC_MAP, &fd_data);
	if (ret < 0) {
		printf("ion map error\n");
		goto free_buf;
	}

	//mmap to user space
	buffer = mmap(NULL, alloc_data.len, PROT_READ|PROT_WRITE, MAP_SHARED,
			fd_data.fd, 0);
	if (MAP_FAILED == buffer) {
		printf("mmap to user space error!\n");
		goto close_dmabuf_fd;
	}

	buffer[0] = '1';
	buffer[1] = '2';
	buffer[2] = '3';
	buffer[3] = '4';
	buffer[4] = '\n';

	printf("client buffer data:%s\n", buffer);
	ret = munmap(buffer, alloc_data.len);
	if (ret < 0) {
		printf("munmap error\n!");
		goto close_dmabuf_fd;
	}

	socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (socket_fd < 0) {
		printf("create socket failed\n");
		ret = socket_fd;
		goto close_dmabuf_fd;
	}

	bzero(&addr, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, "test_socket");
	//addr.sun_path = "test_socket";

	ret = connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr));
	if(ret < 0) {
		printf("connect failed!\n");
		goto close_dmabuf_fd;
	}

	msg.msg_control = control_un.control;
	msg.msg_controllen = sizeof(control_un.control);

	cmptr = CMSG_FIRSTHDR(&msg);
	cmptr->cmsg_len = CMSG_LEN(sizeof(int));
	cmptr->cmsg_level = SOL_SOCKET;
	cmptr->cmsg_type = SCM_RIGHTS;
	*((int *) CMSG_DATA(cmptr)) = fd_data.fd;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	iov[0].iov_base = buf;
	iov[0].iov_len = sizeof(buf)/sizeof(buf[0]);

	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	ret = sendmsg(socket_fd, &msg, 0);
	if (ret < 0) {
		printf("sendmsg failed\n");
		goto close_dmabuf_fd;
	}

	printf("ret :%d, fd:%d\n", ret, fd_data.fd);

close_dmabuf_fd:
	close(fd_data.fd);

free_buf:
	handle_data.handle = alloc_data.handle;
	ret = ioctl(ion_fd, ION_IOC_FREE, &handle_data);
	if (ret)
		printf("ion free buffer error\n");
close_fd:
	close(ion_fd);
	return ret;
}

int main()
{
	int fd;
	int ret = 0;

	printf("start demo\n");
	fd = open("/dev/dmabuf-test", O_RDONLY);
	if (fd < 0) {
		printf("open device faile\n");
		return fd;
	}

	ret = test_share_dmabuf_from_kernel(fd);
	if (ret < 0) {
		printf("test share dmabuf from kernel faile\n");
	}

	ret = test_share_dmabuf_to_kernel(fd);
	if (ret < 0) {
		printf("test share dmabuf to kernel faile\n");
	}

	ret = test_share_dmabuf_in_process();

	close(fd);
	return 0;
}
