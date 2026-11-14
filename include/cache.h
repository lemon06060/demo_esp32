#pragma once

#include<string>
#include<vector>
#include<pthread.h>
#include<sqlite3.h>

class DataCache {
public:
    DataCache(const std::string& db_path = "cache.db");
    ~DataCache();

    // 初始化数据库
    bool init();

    // 保存一份待补发数据
    bool save(const std::string& data);

    // 获取所有待补发数据(id + data)
    struct PendingItem {
        int id;
        std::string data;
        int retry_count;
    };
    std::vector<PendingItem> get_pending(int limit = 100);

    // 删除已补发成功的数据
    bool remove(int id);

    // 增加重试次数
    bool increase_retry(int id);

    // 待补发数量
    int count();

private:
    sqlite3* db_;
    std::string db_path_;
    pthread_mutex_t mutex_; // 互斥锁
};
