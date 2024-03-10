
#ifndef CACHEPOOL_H_
#define CACHEPOOL_H_

#include <condition_variable>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <vector>

#include "hiredis.h"

using std::list;
using std::map;
using std::string;
using std::vector;

#define REDIS_COMMAND_SIZE 300 
#define FIELD_ID_SIZE 100      
#define VALUES_ID_SIZE 1024    
typedef char (
    *RFIELDS)[FIELD_ID_SIZE]; 

typedef char (
    *RVALUES)[VALUES_ID_SIZE]; 

class CachePool;

class CacheConn {
  public:
    CacheConn(const char *server_ip, int server_port, int db_index,
              const char *password, const char *pool_name = "");
    CacheConn(CachePool *pCachePool);
    virtual ~CacheConn();

    int Init();
    void DeInit();
    const char *GetPoolName();
    bool IsExists(string &key);
    long Del(string key);

    string Get(string key);
    string Set(string key, string value);
    string SetEx(string key, int timeout, string value);


    bool MGet(const vector<string> &keys, map<string, string> &ret_value);
    
    int Incr(string key, int64_t &value);
    int Decr(string key, int64_t &value);


    long Hdel(string key, string field);
    string Hget(string key, string field);
    int Hget(string key, char *field, char *value);
    bool HgetAll(string key, map<string, string> &ret_value);
    long Hset(string key, string field, string value);

    long HincrBy(string key, string field, long value);
    long IncrBy(string key, long value);
    string Hmset(string key, map<string, string> &hash);
    bool Hmget(string key, list<string> &fields, list<string> &ret_value);


    long Lpush(string key, string value);
    long Rpush(string key, string value);
    long Llen(string key);
    bool Lrange(string key, long start, long end, list<string> &ret_value);


    int ZsetExit(string key, string member);
    int ZsetAdd(string key, long score, string member);
    int ZsetZrem(string key, string member);
    int ZsetIncr(string key, string member);
    int ZsetZcard(string key);
    int ZsetZrevrange(string key, int from_pos, int end_pos, RVALUES values,
                      int &get_num);
    int ZsetGetScore(string key, string member);

    bool FlushDb();

  private:
    CachePool *cache_pool_;
    redisContext *context_; 
    uint64_t last_connect_time_;
    uint16_t server_port_;
    string server_ip_;
    string password_;
    uint16_t db_index_;
    string pool_name_;
};

class CachePool {
  public:

    CachePool(const char *pool_name, const char *server_ip, int server_port,
              int db_index, const char *password, int max_conn_cnt);
    virtual ~CachePool();

    int Init();

    CacheConn *GetCacheConn(const int timeout_ms = 0);

    void RelCacheConn(CacheConn *cache_conn);

    const char *GetPoolName() { return pool_name_.c_str(); }
    const char *GetServerIP() { return server_ip_.c_str(); }
    const char *GetPassword() { return password_.c_str(); }
    int GetServerPort() { return m_server_port; }
    int GetDBIndex() { return db_index_; }

  private:
    string pool_name_;
    string server_ip_;
    string password_;
    int m_server_port;
    int db_index_; // mysql 数据库名字， redis db index

    int cur_conn_cnt_;
    int max_conn_cnt_;
    list<CacheConn *> free_list_;

    std::mutex m_mutex;
    std::condition_variable cond_var_;
    bool abort_request_ = false;
};

class CacheManager {
  public:
    virtual ~CacheManager();

    static void SetConfPath(const char *conf_path);
    static CacheManager *getInstance();

    int Init();
    CacheConn *GetCacheConn(const char *pool_name);
    void RelCacheConn(CacheConn *cache_conn);

  private:
    CacheManager();

  private:
    static CacheManager *s_cache_manager;
    map<string, CachePool *> m_cache_pool_map;
    static std::string conf_path_;
};

class AutoRelCacheCon {
  public:
    AutoRelCacheCon(CacheManager *manger, CacheConn *conn)
        : manger_(manger), conn_(conn) {}
    ~AutoRelCacheCon() {
        if (manger_) {
            manger_->RelCacheConn(conn_);
        }
    } 
  private:
    CacheManager *manger_ = NULL;
    CacheConn *conn_ = NULL;
};

#define AUTO_REL_CACHECONN(m, c) AutoRelCacheCon autorelcacheconn(m, c)

#endif 