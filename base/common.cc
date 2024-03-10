#include "common.h"
#include "cache_pool.h"
#include <ctype.h>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//去掉空格
int TrimSpace(char *inbuf) {
    int i = 0;
    int j = strlen(inbuf) - 1;
    char *str = inbuf;
    int count = 0;
    if (str == NULL) {
        return -1;
    }
    while (isspace(str[i]) && str[i] != '\0') {
        i++;
    }
    while (isspace(str[j]) && j > i) {
        j--;
    }
    count = j - i + 1;
    strncpy(inbuf, str + i, count);
    inbuf[count] = '\0';
    return 0;
}

//找出query中的key和value
int QueryParseKeyValue(const char *query, const char *key, char *value,
                       int *value_len_p) {
    char *temp = NULL;
    char *end = NULL;
    int value_len = 0;
    temp = (char *)strstr(query, key);
    if (temp == NULL) {
        return -1;
    }
    temp += strlen(key); 
    temp++;              
    end = temp;
    while ('\0' != *end && '#' != *end && '&' != *end) {
        end++;
    }
    value_len = end - temp;
    strncpy(value, temp, value_len);
    value[value_len] = '\0';
    if (value_len_p != NULL) {
        *value_len_p = value_len;
    }
    return 0;
}
//获取文件的后缀
int GetFileSuffix(const char *file_name, char *suffix) {
    const char *p = file_name;
    int len = 0;
    const char *q = NULL;
    const char *k = NULL;
    if (p == NULL) {
        return -1;
    }
    q = p;
    while (*q != '\0') {
        q++;
    }
    k = q;
    while (*k != '.' && k != p) {
        k--;
    }
    if (*k == '.') {
        k++;
        len = q - k;
        if (len != 0) {
            strncpy(suffix, k, len);
            suffix[len] = '\0';
        } else {
            strncpy(suffix, "null", 5);
        }
    } else {
        strncpy(suffix, "null", 5);
    }
    return 0;
}
//登录token数据库
int VerifyToken(string &user_name, string &token) {
    int ret = 0;
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    if (cache_conn) {
        string tmp_token = cache_conn->Get(user_name);
        if (tmp_token == token) {
            ret = 0;
        } else {
            ret = -1;
        }
    } else {
        ret = -1;
    }

    return ret;
}
//随机生成字符串
string RandomString(const int len)
{

    string str; 
    char c;   
    int idx;    
    for (idx = 0; idx < len; idx++) {
        c = 'a' + rand() % 26;
        str.push_back(c); 
    }
    return str;
}

template <typename... Args>
std::string FormatString(const std::string &format, Args... args) {
    auto size = std::snprintf(nullptr, 0, format.c_str(), args...) +
                1; // Extra space for '\0'
    std::unique_ptr<char[]> buf(new char[size]);
    std::snprintf(buf.get(), size, format.c_str(), args...);
    return std::string(buf.get(),
                       buf.get() + size - 1); 
}
//处理数据库查询结果，结果集保存在count，如果要读取count值则设置为0，如果设置为-1则不读取
//返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
int GetResultOneCount(CDBConn *db_conn, char *sql_cmd, int &count) {
    int ret = -1;
    CResultSet *result_set = db_conn->ExecuteQuery(sql_cmd);

    if (!result_set) {
        ret = -1;
    }

    if (count == 0) {

        if (result_set->Next()) {
            ret = 0;
            count = result_set->GetInt("count");
            LogDebug("count: {}", count);
        } else {
            ret = 1; 
        }
    } else {
        if (result_set->Next()) {
            ret = 2;
        } else {
            ret = 1; 
        }
    }

    delete result_set;

    return ret;
}

int CheckwhetherHaveRecord(CDBConn *db_conn, char *sql_cmd) {
    int ret = -1;
    CResultSet *result_set = db_conn->ExecuteQuery(sql_cmd);

    if (!result_set) {
        ret = -1;
    } else if (result_set && result_set->Next()) {
        ret = 1;
    } else {
        ret = 0;
    }

    delete result_set;

    return ret;
}

int GetResultOneStatus(CDBConn *db_conn, char *sql_cmd, int &shared_status) {
    int ret = 0;
    CResultSet *result_set = db_conn->ExecuteQuery(sql_cmd);

    if (!result_set) {
        LogError("result_set is NULL");
        ret = -1;
    }

    if (result_set->Next()) {
        ret = 0;
        shared_status = result_set->GetInt("shared_status");
        LogInfo("shared_status: {}", shared_status);
    } else {
        LogError("result_set->Next() is NULL");
        ret = -1;
    }

    delete result_set;

    return ret;
}

