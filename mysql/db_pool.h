#ifndef DBPOOL_H_
#define DBPOOL_H_
#include <condition_variable>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <stdint.h>
#include <string>
#include <mysql.h>

#define MAX_ESCAPE_STRING_LEN 10240

using namespace std;
//存储结构集
class CResultSet {
  public:
    CResultSet(MYSQL_RES *res);
    virtual ~CResultSet();

    bool Next();
    int GetInt(const char *key);
    char *GetString(const char *key);

  private:
    int _GetIndex(const char *key);
    MYSQL_RES *res_;
    MYSQL_ROW row_;
    map<string, int> key_map_;
};
//用于预处理
class CPrepareStatement {
  public:
    CPrepareStatement();
    virtual ~CPrepareStatement();

    bool Init(MYSQL *mysql, string &sql);

    void SetParam(uint32_t index, int &value);
    void SetParam(uint32_t index, uint32_t &value);
    void SetParam(uint32_t index, string &value);
    void SetParam(uint32_t index, const string &value);

    bool ExecuteUpdate();
    uint32_t GetInsertId();

  private:
    MYSQL_STMT *stmt_;
    MYSQL_BIND *param_bind_;
    uint32_t param_cnt_;
};

class CDBPool;
//mysql接口封装
class CDBConn {
  public:
    CDBConn(CDBPool *pDBPool);
    virtual ~CDBConn();
    int Init();

    bool ExecuteCreate(const char *sql_query);
    bool ExecuteDrop(const char *sql_query);
    CResultSet *ExecuteQuery(const char *sql_query);

    bool ExecutePassQuery(const char *sql_query);
    bool ExecuteUpdate(const char *sql_query, bool care_affected_rows = true);
    uint32_t GetInsertId();

    bool StartTransaction();
    bool Commit();
    bool Rollback();
    const char *GetPoolName();
    MYSQL *GetMysql() { return mysql_; }
    int GetRowNum() { return row_num; }

  private:
    int row_num = 0;
    CDBPool *db_pool_; 
    MYSQL *mysql_;     
    char escape_string_[MAX_ESCAPE_STRING_LEN + 1];
};
//mysql链接池
class CDBPool { 
  public:
    CDBPool() {
    } 
    CDBPool(const char *pool_name, const char *db_server_ip,
            uint16_t db_server_port, const char *username, const char *password,
            const char *db_name, int max_conn_cnt);
    virtual ~CDBPool();

    int Init(); 
    CDBConn *GetDBConn(const int timeout_ms = 0); 
    void RelDBConn(CDBConn *pConn);               

    const char *GetPoolName() { return pool_name_.c_str(); }
    const char *GetDBServerIP() { return db_server_ip_.c_str(); }
    uint16_t GetDBServerPort() { return db_server_port_; }
    const char *GetUsername() { return username_.c_str(); }
    const char *GetPasswrod() { return password_.c_str(); }
    const char *GetDBName() { return db_name_.c_str(); }

  private:
    string pool_name_;          
    string db_server_ip_;       
    uint16_t db_server_port_;   
    string username_;           
    string password_;           
    string db_name_;            
    int db_cur_conn_cnt_;       
    int db_max_conn_cnt_;       
    list<CDBConn *> free_list_; 

    list<CDBConn *> used_list_; 
    std::mutex mutex_;
    std::condition_variable cond_var_;
    bool abort_request_ = false;
};
//管理不同链接的池子
class CDBManager {
  public:
    virtual ~CDBManager();

    static void SetConfPath(const char *conf_path);
    static CDBManager *getInstance();

    int Init();

    CDBConn *GetDBConn(const char *dbpool_name);
    void RelDBConn(CDBConn *pConn);

  private:
    CDBManager();

  private:
    static CDBManager *s_db_manager;
    map<string, CDBPool *> dbpool_map_;
    static std::string conf_path_;
};
//实现资源自动回收
class AutoRelDBCon {
  public:
    AutoRelDBCon(CDBManager *manger, CDBConn *conn)
        : manger_(manger), conn_(conn) {}
    ~AutoRelDBCon() {
        if (manger_) {
            manger_->RelDBConn(conn_);
        }
    } //在析构函数规划
  private:
    CDBManager *manger_ = NULL;
    CDBConn *conn_ = NULL;
};

#define AUTO_REL_DBCONN(m, c) AutoRelDBCon autoreldbconn(m, c)

#endif