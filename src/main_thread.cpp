#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>		// 多线程头文件

// 线程函数：专门处理单个客户端的通信
void* client_handler(void* arg)
{
	// 获取传进来的客户端socket fd
	int client_fd = *(int*)arg;
	delete (int*)arg;		// 释放内存，防止泄露

	char buffer[1024] = {0};
	// 接受数据（recv本身是阻塞的，但在独立线程中不影响主线程）
	int bytes_read = recv(client_fd, buffer, sizeof(buffer)-1, 0);
	if(bytes_read > 0)
	{
		std::cout << "Received: " << buffer << std::endl;
		send(client_fd, "OK\n", 3, 0);
	}	

	close(client_fd);
	std::cout << "Connection closed." << std::endl;
	return nullptr;
}

int main()
{
	// 1. 创建监听的socket(IPv4, TCP)
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd == -1)
	{
		perror("socket create failed");
		return -1;
	}

	// 2. 设置端口复用（防止程序退出后Address already in use）
	int opt = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	// 3. 绑定地址和端口
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(8888);		// 端口 8888
	addr.sin_addr.s_addr = INADDR_ANY;	// 0.0.0.

	if(bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
	{
		perror("bind failed");
		close(listen_fd);
		return -1;
	}

	// 4. 开始监听（最大等待队列长度5）
	if (listen(listen_fd, 5) == -1)
	{
		perror("listen failed");
		close(listen_fd);
		return -1;
	}

	std::cout << "Gateway started on port 8888, waiting for connections..." << std::endl;

	while (true)
	{
		// 5. 接受客户端连接（这一步会阻塞）
		struct sockaddr_in client_addr;
		socklen_t client_len = sizeof(client_addr);
		
		int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
		if (client_fd == -1)
		{
			perror("accept failed");
			continue;
		}

		// 打印客户端IP和端口
		char ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
		std::cout << "New connection from " << ip << ":" << ntohs(client_addr.sin_port) << std::endl;

		/*
		// 6. 接收数据（阻塞）
		char buffer[1024] = {0};
		int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
		if (bytes_read > 0)
		{
			std::cout << "Received: " << buffer << std::endl;
			// 回复OK
			send(client_fd, "OK\n", 3, 0);
		}
		
		// 7. 关闭客户端连接
		close(client_fd);
		std::cout << "Connection closed." << std::endl;

		*/


		// 多线程，修改
		// 把client_fd包装成指针，传给线程
		int* fd_ptr = new int(client_fd);
		
		pthread_t tid;
		// 创建线程，执行client_handler, 把fd_ptr传进去
		pthread_create(&tid, nullptr, client_handler, fd_ptr);
		// 将线程设为“分离状态”：线程结束后自动回收资源，主线程不用等它
		pthread_detach(tid);

	}

	close(listen_fd);

	return 0;
}
