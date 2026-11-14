#include "cache.h"
#include "logger.h"
#include <ctime>
#include <sstream>

extern Logger g_logger;

DataCache::DataCache(const std::string& db_path)
    : db_(nullptr), db_path_(db_path) {
    // 初始化锁
    pthread_mutex_init(&mutex_, nullptr);
}

DataCache::~DataCache() {
    if(db_) {
        sqlite3_close(db_);
    }
    // 销毁锁
    pthread_mutex_destroy(&mutex_);
}

bool DataCache::init() {
    // 加锁
    pthread_mutex_lock(&mutex_);

    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        g_logger.error("Cannot open cache db: " + std::string(sqlite3_errmsg(db_)));
        return false;
    }

    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS pending_data (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            data TEXT NOT NULL,
            created_at TEXT NOT NULL,
            retry_count INTEGER DEFAULT 0
        );
    )";


    char* err_msg = nullptr;
    rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        g_logger.error("Failed to create cache table: " + std::string(err_msg));
        sqlite3_free(err_msg);
        return false;
    }

    // 解锁
    pthread_mutex_unlock(&mutex_);

    g_logger.info("Cache database initialized: " + db_path_);
    return true;
}

bool DataCache::save(const std::string& data) {
    const char* sql = "INSERT INTO pending_data (data, created_at) VALUES (?, datetime('now', 'localtime'));";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        g_logger.error("Failed to prepare insert: " + std::string(sqlite3_errmsg(db_)));
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, data.c_str(), -1, SQLITE_TRANSIENT);
    
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    
    if (ok) {
        g_logger.info("Data cached locally (total pending: " + std::to_string(count()) + ")");
    } else {
        g_logger.error("Failed to cache data: " + std::string(sqlite3_errmsg(db_)));
    }
    return ok;
}

std::vector<DataCache::PendingItem> DataCache::get_pending(int limit) {
    std::vector<PendingItem> items;
    const char* sql = "SELECT id, data, retry_count FROM pending_data ORDER BY id ASC LIMIT ?;";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return items;
    }
    
    sqlite3_bind_int(stmt, 1, limit);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PendingItem item;
        item.id = sqlite3_column_int(stmt, 0);
        item.data = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item.retry_count = sqlite3_column_int(stmt, 2);
        items.push_back(item);
    }
    
    sqlite3_finalize(stmt);
    return items;
}

bool DataCache::remove(int id) {
    const char* sql = "DELETE FROM pending_data WHERE id = ?;";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, id);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool DataCache::increase_retry(int id) {
    const char* sql = "UPDATE pending_data SET retry_count = retry_count + 1 WHERE id = ?;";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, id);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

int DataCache::count() {
    const char* sql = "SELECT COUNT(*) FROM pending_data;";
    sqlite3_stmt* stmt;
    int cnt = 0;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            cnt = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return cnt;
}
