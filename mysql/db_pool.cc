#include "db_pool.h"
#include <string.h>
#include "dlog.h"
#include "config_file_reader.h"

#define MIN_DB_CONN_CNT 1
#define MAX_DB_CONN_FAIL_NUM 10

//单例模式
CDBManager *CDBManager::s_db_manager = NULL;
std::string CDBManager::conf_path_ = "http_server.conf";
//用mysql查询语句返回的结果集构建对象，将mysql表中的列名加入map
CResultSet::CResultSet(MYSQL_RES *res) {
    res_ = res;
    int num_fields = mysql_num_fields(res_); 
    MYSQL_FIELD *fields = mysql_fetch_fields(res_); 
    for (int i = 0; i < num_fields; i++) {
        key_map_.insert(make_pair(fields[i].name,
                                  i)); 
        LogDebug(" num_fields fields[{}].name: {}", i, fields[i].name);
    }
}

CResultSet::~CResultSet() {
    if (res_) {
        mysql_free_result(res_);
        res_ = NULL;
    }
}
//下一行是否有数据
bool CResultSet::Next() {
    row_ = mysql_fetch_row(res_); 
    if (row_) {
        return true;
    } else {
        return false;
    }
}
//通过列名找是第几列
int CResultSet::_GetIndex(const char *key) {
    map<string, int>::iterator it = key_map_.find(key);
    if (it == key_map_.end()) {
        return -1;
    } else {
        return it->second;
    }
}
//通过表的第几列找返回结果集中行的数据
int CResultSet::GetInt(const char *key) {
    int idx = _GetIndex(key); // 查找列的索引
    if (idx == -1) {
        return 0;
    } else {
        return atoi(row_[idx]); 
    }
}
//通过表的第几列找返回结果集中行的数据
char *CResultSet::GetString(const char *key) {
    int idx = _GetIndex(key);
    if (idx == -1) {
        return NULL;
    } else {
        return row_[idx]; // 列
    }
}


CPrepareStatement::CPrepareStatement() {
    stmt_ = NULL;
    param_bind_ = NULL;
    param_cnt_ = 0;
}

CPrepareStatement::~CPrepareStatement() {
    if (stmt_) {
        mysql_stmt_close(stmt_);
        stmt_ = NULL;
    }

    if (param_bind_) {
        delete[] param_bind_;
        param_bind_ = NULL;
    }
}

//进行预处理语句设置 以及初始化参数个数和 将参数列表同时至0
bool CPrepareStatement::Init(MYSQL *mysql, string &sql) {
    mysql_ping(mysql); 
    stmt_ = mysql_stmt_init(mysql);
    if (!stmt_) {
        LogError("mysql_stmt_init failed");
        return false;
    }

    if (mysql_stmt_prepare(stmt_, sql.c_str(), sql.size())) {
        LogError("mysql_stmt_prepare failed: {}", mysql_stmt_error(stmt_));

        return false;
    }

    param_cnt_ = mysql_stmt_param_count(stmt_);
    if (param_cnt_ > 0) {
        param_bind_ = new MYSQL_BIND[param_cnt_];
        if (!param_bind_) {
            LogError("new failed");
            return false;
        }

        memset(param_bind_, 0, sizeof(MYSQL_BIND) * param_cnt_);
    }

    return true;
}
//设置预处理的参数
void CPrepareStatement::SetParam(uint32_t index, int &value) {
    if (index >= param_cnt_) {
        LogError("index too large: {}", index);
        return;
    }

    param_bind_[index].buffer_type = MYSQL_TYPE_LONG;
    param_bind_[index].buffer = &value;
}

void CPrepareStatement::SetParam(uint32_t index, uint32_t &value) {
    if (index >= param_cnt_) {
        LogError("index too large: {}", index);
        return;
    }

    param_bind_[index].buffer_type = MYSQL_TYPE_LONG;
    param_bind_[index].buffer = &value;
}

void CPrepareStatement::SetParam(uint32_t index, string &value) {
    if (index >= param_cnt_) {
        LogError("index too large: {}", index);
        return;
    }

    param_bind_[index].buffer_type = MYSQL_TYPE_STRING;
    param_bind_[index].buffer = (char *)value.c_str();
    param_bind_[index].buffer_length = value.size();
}

void CPrepareStatement::SetParam(uint32_t index, const string &value) {
    if (index >= param_cnt_) {
        LogError("index too large: {}", index);
        return;
    }

    param_bind_[index].buffer_type = MYSQL_TYPE_STRING;
    param_bind_[index].buffer = (char *)value.c_str();
    param_bind_[index].buffer_length = value.size();
}
//预处理参数的绑定且执行预处理语句
bool CPrepareStatement::ExecuteUpdate() {
    if (!stmt_) {
        LogError("no m_stmt"); 
        return false;
    }

    if (mysql_stmt_bind_param(stmt_, param_bind_)) {
        LogError("mysql_stmt_bind_param failed: {}", mysql_stmt_error(stmt_));
        return false;
    }

    if (mysql_stmt_execute(stmt_)) {
        LogError("mysql_stmt_execute failed: {}", mysql_stmt_error(stmt_));
        return false;
    }

    if (mysql_stmt_affected_rows(stmt_) == 0) {
        LogError("ExecuteUpdate have no effect"); 
        return false;
    }

    return true;
}
//为在执行预处理语句期间自动生成或明确设置的AUTO_INCREMENT列返回值，
uint32_t CPrepareStatement::GetInsertId() {
    return mysql_stmt_insert_id(stmt_);
}


CDBConn::CDBConn(CDBPool *pPool) {
    db_pool_ = pPool;
    mysql_ = NULL;
}

CDBConn::~CDBConn() {
    if (mysql_) {
        mysql_close(mysql_);
    }
}

int CDBConn::Init() {
    mysql_ = mysql_init(NULL); 
    if (!mysql_) {
        LogError("mysql_init failed"); 

        return 1;
    }

    int reconnect = 1;
    mysql_options(mysql_, MYSQL_OPT_RECONNECT,
                  &reconnect); 
    mysql_options(mysql_, MYSQL_SET_CHARSET_NAME,
                  "utf8mb4"); 

    if (!mysql_real_connect(mysql_, db_pool_->GetDBServerIP(),
                            db_pool_->GetUsername(), db_pool_->GetPasswrod(),
                            db_pool_->GetDBName(), db_pool_->GetDBServerPort(),
                            NULL, 0)) {
        LogError("mysql_real_connect failed: {}", mysql_error(mysql_));
        return 2;
    }

    return 0;
}

const char *CDBConn::GetPoolName() { return db_pool_->GetPoolName(); }

bool CDBConn::ExecuteCreate(const char *sql_query) {
    mysql_ping(mysql_);
    if (mysql_real_query(mysql_, sql_query, strlen(sql_query))) {
        LogError("mysql_real_query failed: {}", mysql_error(mysql_)); 
        return false;
    }

    return true;
}

bool CDBConn::ExecutePassQuery(const char *sql_query) {
    mysql_ping(mysql_);
    if (mysql_real_query(mysql_, sql_query, strlen(sql_query))) {
        LogError("mysql_real_query failed: {}", mysql_error(mysql_)); 
        return false;
    }

    return true;
}

bool CDBConn::ExecuteDrop(const char *sql_query) {
    mysql_ping(mysql_); 
    if (mysql_real_query(mysql_, sql_query, strlen(sql_query))) {
        LogError("mysql_real_query failed: {}", mysql_error(mysql_)); 
        return false;
    }

    return true;
}
//执行语句的同时创建结果集对象
CResultSet *CDBConn::ExecuteQuery(const char *sql_query) {
    mysql_ping(mysql_);
    row_num = 0;
    if (mysql_real_query(mysql_, sql_query, strlen(sql_query))) {
        LogError("mysql_real_query failed: {}, sql:{}",  mysql_error(mysql_), sql_query);
        return NULL;
    }
    MYSQL_RES *res = mysql_store_result(
        mysql_); 
    if (!res) 
    {
        LogError("mysql_store_result failed: {}", mysql_error(mysql_));
        return NULL;
    }
    row_num = mysql_num_rows(res);
    CResultSet *result_set = new CResultSet(res); 
    return result_set;
}
//判断update操作影响的行数来确定返回值
bool CDBConn::ExecuteUpdate(const char *sql_query, bool care_affected_rows) {
    mysql_ping(mysql_);

    if (mysql_real_query(mysql_, sql_query, strlen(sql_query))) {
        LogError("mysql_real_query failed: {}, sql:{}",  mysql_error(mysql_), sql_query);
        return false;
    }

    if (mysql_affected_rows(mysql_) > 0) {
        return true;
    } else {                      
        if (care_affected_rows) {           
            LogError("mysql_real_query failed: {}, sql:{}",  mysql_error(mysql_), sql_query);
            return false;
        } else {
            LogWarn("affected_rows=0, sql: {}", sql_query);
            return true;
        }
    }
}
//开启事务
bool CDBConn::StartTransaction() {
    mysql_ping(mysql_);

    if (mysql_real_query(mysql_, "start transaction\n", 17)) {
        LogError("mysql_real_query failed: {}, start transaction failed",  mysql_error(mysql_));
        return false;
    }

    return true;
}
//回滚
bool CDBConn::Rollback() {
    mysql_ping(mysql_);

    if (mysql_real_query(mysql_, "rollback\n", 8)) {
        LogError("mysql_real_query failed: {}, sql: rollback", mysql_error(mysql_));
        return false;
    }

    return true;
}
//提交
bool CDBConn::Commit() {
    mysql_ping(mysql_);

    if (mysql_real_query(mysql_, "commit\n", 6)) {
        LogError("mysql_real_query failed: {}, sql: commit",  mysql_error(mysql_));
        return false;
    }

    return true;
}

uint32_t CDBConn::GetInsertId() { return (uint32_t)mysql_insert_id(mysql_); }


CDBPool::CDBPool(const char *pool_name, const char *db_server_ip,
                 uint16_t db_server_port, const char *username,
                 const char *password, const char *db_name, int max_conn_cnt) {
    pool_name_ = pool_name;
    db_server_ip_ = db_server_ip;
    db_server_port_ = db_server_port;
    username_ = username;
    password_ = password;
    db_name_ = db_name;
    db_max_conn_cnt_ = max_conn_cnt;    
    db_cur_conn_cnt_ = MIN_DB_CONN_CNT; 
}

CDBPool::~CDBPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    abort_request_ = true;
    cond_var_.notify_all(); //唤醒所有线程，通知已经停止了

    for (list<CDBConn *>::iterator it = free_list_.begin();
         it != free_list_.end(); it++) {
        CDBConn *pConn = *it;
        delete pConn;
    }

    free_list_.clear();
}
//创建一定数量的链接
int CDBPool::Init() {
    for (int i = 0; i < db_cur_conn_cnt_; i++) {
        CDBConn *db_conn = new CDBConn(this);
        int ret = db_conn->Init();
        if (ret) {
            delete db_conn;
            return ret;
        }

        free_list_.push_back(db_conn);
    }

    return 0;
}

//获取链接，通过大链接数判断是否需要等待，通过timeout_ms来判断等待时长
CDBConn *CDBPool::GetDBConn(const int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (abort_request_) {
        LogWarn("have aboort"); 
        return NULL;
    }

    if (free_list_.empty()) 
    {
        if (db_cur_conn_cnt_ >= db_max_conn_cnt_) 
        {
            if (timeout_ms <= 0) 
            {
                cond_var_.wait(lock, [this] {
                    return (!free_list_.empty()) | abort_request_;
                });
            } else {
                cond_var_.wait_for(
                    lock, std::chrono::milliseconds(timeout_ms),
                    [this] { return (!free_list_.empty()) | abort_request_; });
                if (free_list_.empty()) 
                {
                    return NULL;
                }
            }

            if (abort_request_) {
                LogWarn("have abort"); 
                return NULL;
            }
        } else
        {
            CDBConn *db_conn = new CDBConn(this);
            int ret = db_conn->Init();
            if (ret) {
                LogError("Init DBConnecton failed"); 
                delete db_conn;
                return NULL;
            } else {
                free_list_.push_back(db_conn);
                db_cur_conn_cnt_++;
            }
        }
    }

    CDBConn *pConn = free_list_.front();
    free_list_.pop_front();

    return pConn;
}
//回收链接进入list
void CDBPool::RelDBConn(CDBConn *pConn) {
    std::lock_guard<std::mutex> lock(mutex_);

    list<CDBConn *>::iterator it = free_list_.begin();
    for (; it != free_list_.end(); it++)
    {
        if (*it == pConn) {
            break;
        }
    }

    if (it == free_list_.end()) {
        free_list_.push_back(pConn);
        cond_var_.notify_one();
    } else {
        LogWarn("RelDBConn failed"); 
    }
}

CDBManager::CDBManager() {}

CDBManager::~CDBManager() {}
//单例模式
CDBManager *CDBManager::getInstance() {
    if (!s_db_manager) {
        s_db_manager = new CDBManager();
        if (s_db_manager->Init()) {
            delete s_db_manager;
            s_db_manager = NULL;
        }
    }

    return s_db_manager;
}

void CDBManager::SetConfPath(const char *conf_path)
{
    conf_path_ = conf_path;
}

int CDBManager::Init() {
    CConfigFileReader config_file(conf_path_.c_str());

    char *db_instances = config_file.GetConfigName("DBInstances");

    if (!db_instances) {
        LogError("not configure DBInstances"); 
        return 1;
    }

    char host[64];
    char port[64];
    char dbname[64];
    char username[64];
    char password[64];
    char maxconncnt[64];
    CStrExplode instances_name(db_instances, ',');

    for (uint32_t i = 0; i < instances_name.GetItemCnt(); i++) {
        char *pool_name = instances_name.GetItem(i);
        snprintf(host, 64, "%s_host", pool_name);
        snprintf(port, 64, "%s_port", pool_name);
        snprintf(dbname, 64, "%s_dbname", pool_name);
        snprintf(username, 64, "%s_username", pool_name);
        snprintf(password, 64, "%s_password", pool_name);
        snprintf(maxconncnt, 64, "%s_maxconncnt", pool_name);

        char *db_host = config_file.GetConfigName(host);
        char *str_db_port = config_file.GetConfigName(port);
        char *db_dbname = config_file.GetConfigName(dbname);
        char *db_username = config_file.GetConfigName(username);
        char *db_password = config_file.GetConfigName(password);
        char *str_maxconncnt = config_file.GetConfigName(maxconncnt);

        LogInfo("db_host:{}, db_port:{}, db_dbname:{}, db_username:{}, db_password:{}", 
            db_host, str_db_port, db_dbname, db_username, db_password);

        if (!db_host || !str_db_port || !db_dbname || !db_username ||
            !db_password || !str_maxconncnt) {
            LogError("not configure db instance: {}", pool_name);
            return 2;
        }

        int db_port = atoi(str_db_port);
        int db_maxconncnt = atoi(str_maxconncnt);
        CDBPool *pDBPool = new CDBPool(pool_name, db_host, db_port, db_username,
                                       db_password, db_dbname, db_maxconncnt);
        if (pDBPool->Init()) {
            LogError("init db instance failed: {}", pool_name);
            return 3;
        }
        dbpool_map_.insert(make_pair(pool_name, pDBPool));
    }

    return 0;
}
//通过链接池的名字找到连接池  再获取链接
CDBConn *CDBManager::GetDBConn(const char *dbpool_name) {
    map<string, CDBPool *>::iterator it = dbpool_map_.find(dbpool_name); // 主从
    if (it == dbpool_map_.end()) {
        return NULL;
    } else {
        return it->second->GetDBConn();
    }
}

void CDBManager::RelDBConn(CDBConn *pConn) {
    if (!pConn) {
        return;
    }

    map<string, CDBPool *>::iterator it = dbpool_map_.find(pConn->GetPoolName());
    if (it != dbpool_map_.end()) {
        it->second->RelDBConn(pConn);
    }
}
