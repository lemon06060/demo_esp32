#include "../include/thread_pool.h"
#include "logger.h"
#include "json.hpp"
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <curl/curl.h>
//#include <nlohmann/json.hpp>
#include <cstring>
#include <string>

using json = nlohmann::json;

// 引用外部变量
extern Logger g_logger;
extern std::string g_cloud_url;

ThreadPool::ThreadPool(int size)
    : stop(false)
    , pool_size(size)
{
    // 初始化缓存
    cache_ = new DataCache("cache.db");
    if (!cache_->init()) {
        g_logger.error("Cache init failed");
    }


    // 初始化互斥锁和条件变量
    pthread_mutex_init(&mutex, nullptr);
    pthread_cond_init(&cond, nullptr);

    // 创建工作线程
    workers.resize(pool_size);
    for (int i = 0; i < pool_size; i++) {
        // 注意： pthread_create需要静态函数，所以用worker_func
        if (pthread_create(&workers[i], nullptr, worker_func, this) != 0) {
            g_logger.error("Failed to create worker thread" + std::to_string(i));
        }
    }

    // 启动补发线程
    start_retry_thread();

    g_logger.info("ThreadPool initialized with " + std::to_string(pool_size) + " threads");

}

ThreadPool::~ThreadPool() {
    // 通知所有线程停止
    stop = true;
    retry_running_ = false;
    pthread_cond_broadcast(&cond);

    // 等待补发线程结束
    if (retry_thread_) {
        pthread_join(retry_thread_, nullptr);
    }

    // 等待所有线程完成
    for (auto& t : workers) {
        pthread_join(t, nullptr);
    }

    // 销毁同步对象
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);

    delete cache_;
    g_logger.info("ThreadPool destroyed");
}

// 添加任务
void ThreadPool::add_task(int client_fd) {
    pthread_mutex_lock(&mutex);
    tasks.push(client_fd);
    pthread_cond_signal(&cond);     // 唤醒一个等待的线程
    pthread_mutex_unlock(&mutex);
}

// 获取待处理任务数
int ThreadPool::get_pending_tasks() {
    pthread_mutex_lock(&mutex);
    int count = tasks.size();
    pthread_mutex_unlock(&mutex);
    return count;
}

// 静态工作线程入口
void* ThreadPool::worker_func(void* arg) {
    ThreadPool* pool = static_cast<ThreadPool*>(arg);
    pool->worker_loop();
    return nullptr;
}

// 工作线程主循环
void ThreadPool::worker_loop() {
    while (true) {
        // 获取任务
        pthread_mutex_lock(&mutex);
        
        // 等待条件：有任务 或 需要停止
        while (tasks.empty() && !stop) {
            pthread_cond_wait(&cond, &mutex);
        }

        // 如果停止且没有任务，退出线程
        if (stop && tasks.empty()) {
            pthread_mutex_unlock(&mutex);
            break;
        }
    
        // 取出任务
        int client_fd = tasks.front();
        tasks.pop();
        pthread_mutex_unlock(&mutex);

        // 处理客户端（在锁外执行，不影响其他线程获取任务）
        handle_client(client_fd);
    }
}

// 处理客户端
void ThreadPool::handle_client(int client_fd) {
    // 设置socket超时 5s
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    char buffer[4096] = {0};
    int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if ( n <= 0) {
        g_logger.info("Connection closed or error");
        close(client_fd);
        return;
    }

    g_logger.info("Received data from client");
    std::string raw_data(buffer, n);

    try {
        json data = json::parse(raw_data);

        // 处理JSON数据
        std::string device_id = data.value("device_id", "unknown");
        double temperature = data.value("temperature", 0.0);
        double humidity = data.value("humidity", 0.0);
        std::string timestamp = data.value("timestamp", "unknown");

        std::string log_msg = "[Device: " + device_id + "] Temp: " + std::to_string(temperature) + "℃, Humidity: " + std::to_string(humidity) + "%, Time: " + timestamp;
        g_logger.log(Logger::INFO, log_msg);

        // 转发到云端
        if (forward_to_cloud(raw_data)) {
            g_logger.info("Data forwarded to cloud successfully");
        } else {
            // 转发失败，缓存到本地
            g_logger.warn("Cloud unreachable, caching data locally");
            cache_->save(raw_data);
        }


        // 回复客户端
        std::string response = "{\"status\":\"ok\"}\n";
        send(client_fd, response.c_str(), response.length(), 0);
       
    } catch(std::exception& e) {
        g_logger.error("JSON parse error: " + std::string(e.what()));
    }

    close(client_fd);
        
}

// 云端转发
bool ThreadPool::forward_to_cloud(const std::string& data) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        g_logger.error("Failed to init curl");
        return false;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, g_cloud_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    bool success = false;

    if (res != CURLE_OK) {
        g_logger.error("HTTP request failed: " + std::string(curl_easy_strerror(res)));
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if(http_code == 200) {
            g_logger.info("Data forward to cloud successfully");
            success = true;
        } else {
            g_logger.error("HTTP error: " + std::to_string(http_code));
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return success;
}


void ThreadPool::start_retry_thread() {
    retry_running_ = true;
    pthread_create(&retry_thread_, nullptr, retry_func, this);
    g_logger.info("Retry thread started");
}

void* ThreadPool::retry_func(void* arg) {
    ThreadPool* pool = static_cast<ThreadPool*>(arg);
    pool->retry_loop();
    return nullptr;
}

void ThreadPool::retry_loop() {
    while (retry_running_) {
        // 每 30 秒检查一次
        for (int i = 0; i < 30 && retry_running_; i++) {
            sleep(1);
        }
        
        if (!retry_running_) break;
        
        int pending = cache_->count();
        if (pending == 0) continue;
        
        g_logger.info("Retrying " + std::to_string(pending) + " pending items...");
        
        auto items = cache_->get_pending(100);
        for (auto& item : items) {
            if (!retry_running_) break;
            
            if (forward_to_cloud(item.data)) {
                cache_->remove(item.id);
                g_logger.info("Retry success: id=" + std::to_string(item.id));
            } else {
                cache_->increase_retry(item.id);
                g_logger.warn("Retry failed: id=" + std::to_string(item.id) + 
                              ", retry_count=" + std::to_string(item.retry_count + 1));
                break;  // 云端还是不通，等下次再试
            }
        }
    }
}
