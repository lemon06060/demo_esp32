#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>		// 用于waitpid
#include <signal.h>		    // 用于信号处理
#include <curl/curl.h>      // 网络连接
//#include <pthread.h>		// 多线程头文件
//#include "../include/g_logger.h"	// 引入日志
#include "logger.h"	        // 引入日志
#include "json.hpp"	        // 引入json
#include "thread_pool.h"    // 线程池

using json = nlohmann::json;

Logger g_logger("gateway.log");

std::string g_cloud_url = "https://httpbin.org/post";
int g_server_port = 8888;

// 加载配置文件
struct Config {
    int port = 8888;
    std::string cloud_url = "https://httpbin.org/post";
    std::string log_level = "INFO";
};

Config load_config(const std::string& config_path) {
    Config cfg;
    std::ifstream file(config_path);
    if (!file.is_open()) {
        g_logger.log(Logger::INFO, "Config file not found, using defaults");
        return cfg;
    }

    try
    {
        json j;
        file >> j;
        cfg.port = j.value("server_port", 8888);
        cfg.cloud_url = j.value("cloud_url", "https://httpbin.org/post");
        cfg.log_level = j.value("log_level", "INFO");
        g_logger.log(Logger::INFO, "Config loaded: port=" + std::to_string(cfg.port) + ",url=" + cfg.cloud_url);

    } catch (const std::exception& e) {
        g_logger.log(Logger::ERROR, "Failed to prase config.json, using defaults: " + std::string(e.what()));
    }
}

// ======== HTTP 转发函数 =========
void forward_to_cloud(const std::string& json_data, const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        g_logger.log(Logger::ERROR, "Failed to init curl");
        return;
    }

    // 目标服务器（用httpbin.org 做测试，它会回显你的请求
    //std::string url = "https://httpbin.org/post";

    // 设置URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // 设置 POST 数据
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());

    // 设置HTTP头：告诉服务器这是JSON
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // 执行请求 （阻塞，等待服务器响应）
    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        g_logger.log(Logger::INFO, "Data forwarded to cloud successfuly");
    } else {
        g_logger.log(Logger::ERROR, "Failed to forward data: " + std::string(curl_easy_strerror(res)));
    }

    // 清理
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}


// 多进程   信号处理函数：回收僵尸子进程
void sigchld_handler(int signo)
{
	// 循环回收所有已结束的子进程，防止僵尸进程
	while(waitpid(-1, nullptr, WNOHANG) > 0);
	g_logger.log(Logger::INFO, "Child process reaped");
}

// 多线程
void* handle_client_thread(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);

    char buffer[4096] = {0};
    int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (n <= 0) {
        g_logger.log(Logger::INFO, "Connection closed or error");
        close(client_fd);
        return nullptr;
    }

    g_logger.log(Logger::INFO, "Received data from client");

    try {
        std::string raw_data(buffer, n);
        json data = json::parse(raw_data);

        // 处理JSON数据
        std::string device_id = data.value("device_id", "unknown");
        double temperature = data.value("temperature", 0.0);
        double humidity = data.value("humidity", 0.0);
        std::string timestamp = data.value("timestamp", "unknown");

        std::string log_msg = "[Device: " + device_id + "] Temp: " + std::to_string(temperature) + "℃, Humidity: " + std::to_string(humidity) + "%, Time: " + timestamp;
        g_logger.log(Logger::INFO, log_msg);

        // 转发到云端
        forward_to_cloud(raw_data, g_cloud_url);

        // 回复客户端
        std::string response = "{\"status\":\"ok\"}\n";
        send(client_fd, response.c_str(), response.length(), 0);
        
    } catch (const std::exception& e) {
        std::string error = "{\"status\":\"error\",\"msg\":\"Invalid JSON\"}\n";
        send(client_fd, error.c_str(), error.length(), 0);
        g_logger.log(Logger::ERROR, "JSON parse error: " + std::string(e.what()));
    }

    close(client_fd);
    return nullptr;
}


int main()
{
	// 1. 设置SIGCHLD信号处理函数（防止子进程变成僵尸）
	signal(SIGCHLD, sigchld_handler);

	// 2. 创建监听 socket
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd == -1)
	{
		g_logger.log(Logger::ERROR, "socket create failed");
		perror("socket create failed");
		return -1;
	}

	// 3. 设置端口复用
	int opt = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	// 4. 绑定地址
    // 加载配置
    
    Config cfg = load_config("config.json");

    g_cloud_url = cfg.cloud_url;
    g_server_port = cfg.port;

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(cfg.port);
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
	{
		g_logger.log(Logger::ERROR, "bind failed");
		perror("bind failed");
		close(listen_fd);
		return -1;
	}

	// 5. 监听
	if (listen(listen_fd, 5) == -1)
	{
		g_logger.log(Logger::ERROR, "listen failed");
		perror("listen failed");
		close(listen_fd);
		return -1;
	}

    g_logger.log(Logger::INFO, "Gateway started on port " + std::to_string(cfg.port));

    // 线程池
    ThreadPool pool(5);

	//std::cout << "Multi-Process Gateway started on port " << std::to_string(cfg.port) << "...." << std::endl;

	// 6. 主循环：接受连接 -> 创建子进程处理
	while (true)
	{
		struct sockaddr_in client_addr;
		socklen_t client_len = sizeof(client_addr);

		int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
		if (client_fd == -1)
		{
			g_logger.log(Logger::ERROR, "accept failed");
			perror("accept failed");
			continue;
		}

        
		char ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));

		std::string log_msg = "New connection from " + std::string(ip) + ":" + std::to_string(ntohs(client_addr.sin_port));

		std::cout << log_msg << std::endl;
		g_logger.log(Logger::INFO, log_msg);
	    
        // *** 线程池 ***
        pool.add_task(client_fd);
        	
#if 1
       
        
        
#elif 2
        // *** 多线程 ***
        
        // 创建线程处理客户端
        int* pclient = (int*)malloc(sizeof(int));
        *pclient = client_fd;

        pthread_t tid;
        if(pthread_create(&tid, nullptr, handle_client_thread, pclient)!= 0) {
            g_logger.log(Logger::ERROR, "Failed to create thread");
            free(pclient);
            close(client_fd);
            continue;
        }

        pthread_detach(tid);    // 自动回收线程资源

#else
		// *** 核心：创建子进程 ***
        pid_t pid = fork();
		if (pid == -1)
		{
			g_logger.log(Logger::ERROR, "fork failed");
			// fork 失败
			perror("fork failed");
			close(client_fd);
			continue;
		}

		if (pid == 0)
		{
			// *** 子进程代码 ***
			// 子进程复制了父进程的所有内存和id
			// 1. 关闭子进程不需要的 listen_fd (不关也行，但习惯上关掉)
			close(listen_fd);
			
			// 2. 处理客户端通信
			char buffer[1024] = {0};
			int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
			if (bytes_read > 0)
			{
                // *** JSON解析 ***
                std::string raw_data(buffer);
                
                // 去除末尾换行符
                while (!raw_data.empty() && (raw_data.back() == '\n' || raw_data.back() == '\r')) {
                    raw_data.pop_back();
                }

                try {
                    json data = json::parse(raw_data);

                    std::string device_id = data.value("device_id", "unknown");
                    double temperature = data.value("temperature", 0.0);
                    double humidity = data.value("humidity", 0.0);
                    std::string timestamp = data.value("timestamp", "unknown");

                    std::string log_msg = "[Device: " + device_id + "] Temp: " + std::to_string(temperature) + "℃, Humidity: " + std::to_string(humidity) + "%, Time: " + timestamp;
                    g_logger.log(Logger::INFO, log_msg);


                    // *** 新增：把原始JSON数据转发到云端
                    forward_to_cloud(raw_data, cfg.cloud_url);

                    std::string response = "{\"status\":\"ok\",\"device\":\"" + device_id + "\"}\n";
                    send(client_fd, response.c_str(), response.length(), 0);


                } catch (const json::parse_error& e) {
                    g_logger.log(Logger::WARN, "Non-JSON data received: " + raw_data);

                    send(client_fd, "{\"status\":\"error\",\"msg\":\"invalid json\"}\n", 100, 0);

                }

                /*
				std::string data_msg = "Received: " + std::string(buffer);
				g_logger.log(Logger::INFO, data_msg);

				std::cout << data_msg << std::endl;
				send(client_fd, "OK\n", 3, 0);
                */
			} else
			{
				g_logger.log(Logger::WARN, "Client disconnected or error");
				std::cout << "Client disconnected or error." << std::endl;
			}

			// 3. 关闭客户端 socket
			close(client_fd);

			// 4. 子进程退出
			std::cout << "[SubProcess] Connection closed. Exiting." << std::endl;
				
			g_logger.log(Logger::INFO, "Connection closed");
			exit(0);
		}
		else
		{
			// *** 父进程代码 ***
			// 1. 父进程用不到client_fd，必须关闭！
			close(client_fd);
		
			g_logger.log(Logger::INFO, "父进程调用");
			// 2. 父进程不阻塞，立刻回到while循环继续 accept
			// 注：子进程的回收由 sigchld_handler处理
			continue;
		}
#endif
	}

	close(listen_fd);

	return 0;
}


// 线程函数：专门处理单个客户端的通信
void* client_handler(void* arg)
{
	// 获取传进来的
	// 客户端socket fd
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

int main_thread()
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
