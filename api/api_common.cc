#include "api_common.h"

string s_dfs_path_client;
string s_storage_web_server_ip;
string s_storage_web_server_port;
string s_shorturl_server_address;
string s_shorturl_server_access_token;
//将字符串拼接写入string
template <typename... Args>
static std::string FormatString(const std::string &format, Args... args) {
    auto size = std::snprintf(nullptr, 0, format.c_str(), args...) +
                1; 
    std::unique_ptr<char[]> buf(new char[size]);
    std::snprintf(buf.get(), size, format.c_str(), args...);
    return std::string(buf.get(),
                       buf.get() + size - 1); 
}
//获取共享文件的数量将mysql的写入redis
int ApiInit() {
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    int ret = 0;
    int count = 0;
    ret = DBGetShareFilesCount(db_conn, count);
    if (ret < 0) {
        LogError("GetShareFilesCount failed");
        return -1;
    }
    ret = CacheSetCount(cache_conn, FILE_PUBLIC_COUNT, (int64_t)count);
    if (ret < 0) {
        LogError("CacheSetCount failed");
        return -1;
    }

    return 0;
}

int CacheSetCount(CacheConn *cache_conn, string key, int64_t count) {
    string ret = cache_conn->Set(key, std::to_string(count));
    if (!ret.empty()) {
        return 0;
    } else {
        return -1;
    }
}

int CacheGetCount(CacheConn *cache_conn, string key, int64_t &count) {
    count = 0;
    string str_count = cache_conn->Get(key);
    if (!str_count.empty()) {
        count = atoll(str_count.c_str());
        return 0;
    } else {
        return -1;
    }
}

int CacheIncrCount(CacheConn *cache_conn, string key) {
    int64_t count = 0;
    int ret = cache_conn->Incr(key, count);
    if (ret < 0) {
        return -1;
    }
    LogInfo("{}-{}", key, count);
    
    return 0;
}

int CacheDecrCount(CacheConn *cache_conn, string key) {
    int64_t count = 0;
    int ret = cache_conn->Decr(key, count);
    if (ret < 0) {
        return -1;
    }
    LogInfo("{}-{}", key, count);
    if (count < 0) {
        LogError("{} 请检测你的逻辑 decr  count < 0  -> {}", key , count);
        ret = CacheSetCount(cache_conn, key, 0); // 文件数量最小为0值
        if (ret < 0) {
            return -1;
        }
    }
    return 0;
}

int DBGetUserFilesCountByUsername(CDBConn *db_conn, string user_name,
                                  int &count) {
    count = 0;
    int ret = 0;
    string str_sql;

    str_sql =
        FormatString("select count(*) from user_file_list where user='%s'",
                     user_name.c_str());
    LogInfo("执行: {}", str_sql);
    CResultSet *result_set = db_conn->ExecuteQuery(str_sql.c_str());
    if (result_set && result_set->Next()) {
        count = result_set->GetInt("count(*)");
        LogInfo("count: {}", count);
        ret = 0;
        delete result_set;
    } else if (!result_set) { 
        LogError("{} 操作失败", str_sql);
        LogError("{} 操作失败", str_sql);
        ret = -1;
    } else {
        ret = 0;
        LogInfo("没有记录: count: {}", count);
    }
    return ret;
}

int DBGetSharePictureCountByUsername(CDBConn *db_conn, string user_name,
                                     int &count) {
    count = 0;
    int ret = 0;
    string str_sql;

    str_sql =
        FormatString("select count(*) from share_picture_list where user='%s'",
                     user_name.c_str());
    LogInfo("执行: {}", str_sql);
    CResultSet *result_set = db_conn->ExecuteQuery(str_sql.c_str());
    if (result_set && result_set->Next()) {
        count = result_set->GetInt("count(*)");
        LogInfo("count: {}", count);
        ret = 0;
        delete result_set;
    } else if (!result_set) { 
        LogError("{} 操作失败", str_sql);
        ret = -1;
    } else {
        ret = 0;
        LogInfo("没有记录: count: {}", count);
    }
    return ret;
}

int DBGetShareFilesCount(CDBConn *db_conn, int &count) {
    count = 0;
    int ret = 0;

    string str_sql = "select count(*) from share_file_list";
    CResultSet *result_set = db_conn->ExecuteQuery(str_sql.c_str());
    if (result_set && result_set->Next()) {
        count = result_set->GetInt("count(*)");
        LogInfo("count: {}", count);
        ret = 0;
        delete result_set;
    } else if (!result_set) {
        LogError("{} 操作失败", str_sql);
        ret = -1;
    } else {
        ret = 0;
        LogInfo("没有记录: count: {}", count);
    }

    return ret;
}
//删除路径中指定文件
int RemoveFileFromFastDfs(const char *fileid) {
    int ret = 0;

    char cmd[1024 * 2] = {0};
    sprintf(cmd, "fdfs_delete_file %s %s", s_dfs_path_client.c_str(), fileid);

    ret = system(cmd);
    LogInfo("RemoveFileFromFastDfs ret = {}", ret);

    return ret;
}