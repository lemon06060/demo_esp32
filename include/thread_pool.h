#pragma once

#include <queue>
#include <vector>
#include <pthread.h>
#include <string>
#include "cache.h"

class ThreadPool {
public:
    explicit ThreadPool(int size = 5);
    ~ThreadPool();

    // 添加任务到线程池
    // client_fd 客户端socket文件描述符
    void add_task(int client_fd);

    // 获取线程池大小
    int get_size() const {return pool_size;}

    // 获取当前待处理任务数
    int get_pending_tasks();

private:
    // 工作线程的入口函数(静态方法：因为pthread需要C风格函数指针)
    static void* worker_func(void* arg);

    // 实际的工作循环
    void worker_loop();

    // 处理客户端连接
    void handle_client(int client_fd);

    // 云端转发
    //void forward_to_cloud(const std::string& data);

private:
    std::queue<int> tasks;          // 任务队列（存储client_fd）
    std::vector<pthread_t> workers; // 工作线程ID列表
    pthread_mutex_t mutex;          // 互斥锁 保护任务队列
    pthread_cond_t cond;            // 条件变量 通知线程有任务
    bool stop;                      // 停止标志
    int pool_size;                  // 线程池大小

//-- 新增数据缓存
    DataCache* cache_ = nullptr;
    bool retry_running_ = false;
    pthread_t retry_thread_;
    
    bool forward_to_cloud(const std::string& data);
    void start_retry_thread();
    static void* retry_func(void* arg);
    void retry_loop();

};

