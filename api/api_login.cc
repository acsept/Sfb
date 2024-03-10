#include "api_login.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "http_conn.h"

#define LOGIN_RET_OK 0
#define LOGIN_RET_FAIL 1

// 解析登录信息
int decodeLoginJson(const std::string &str_json, string &user_name,
                    string &pwd) {
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res) {
        LogError("parse reg json failed ");
        return -1;
    }
    // 用户名
    if (root["user"].isNull()) {
        LogError("user null");
        return -1;
    }
    user_name = root["user"].asString();

    //密码
    if (root["pwd"].isNull()) {
        LogError("pwd null");
        return -1;
    }
    pwd = root["pwd"].asString();

    return 0;
}

// 封装登录结果的json
int encodeLoginJson(int ret, string &token, string &str_json) {
    Json::Value root;
    root["code"] = ret;
    if (ret == 0) {
        root["token"] = token;
    }
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}

template <typename... Args>
std::string formatString1(const std::string &format, Args... args) {
    auto size = std::snprintf(nullptr, 0, format.c_str(), args...) +
                1; // Extra space for '\0'
    std::unique_ptr<char[]> buf(new char[size]);
    std::snprintf(buf.get(), size, format.c_str(), args...);
    return std::string(buf.get(),
                       buf.get() + size - 1); // We don't want the '\0' inside
}
//验证账号密码
int verifyUserPassword(string &user_name, string &pwd) {
    int ret = 0;
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);

    // 先查看用户是否存在
    string strSql;

    strSql =
        formatString1("select password from user_info where user_name='%s'",
                      user_name.c_str());
    CResultSet *result_set = db_conn->ExecuteQuery(strSql.c_str());
    uint32_t nowtime = time(NULL);
    if (result_set && result_set->Next()) {
        // 存在再返回
        string password = result_set->GetString("password");
        LogInfo("mysql-pwd: {}, user-pwd: {}", password, pwd);
        if (result_set->GetString("password") == pwd)
            ret = 0;
        else
            ret = -1;
    } else {
        ret = -1;
    }

    delete result_set;

    return ret;
}


int setToken(string &user_name, string &token) {
    int ret = 0;
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    token = RandomString(32); // 随机32个字母

    if (cache_conn) {
        //用户名：token, 86400有效时间为24小时
        cache_conn->SetEx(user_name, 86400, token); // redis做超时
    } else {
        ret = -1;
    }

    return ret;
}

int loadMyfilesCountAndSharepictureCount(string &user_name) {
    int64_t redis_file_count = 0;
    int mysq_file_count = 0;
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    // 从mysql加载
    if (DBGetUserFilesCountByUsername(db_conn, user_name, mysq_file_count) <
        0) {
        LogError("DBGetUserFilesCountByUsername failed");
        return -1;
    }
    redis_file_count = (int64_t)mysq_file_count;
    if (CacheSetCount(cache_conn, FILE_USER_COUNT + user_name,
                      redis_file_count) < 0) 
    {
        LogError("DBGetUserFilesCountByUsername failed");
        return -1;
    }
    LogInfo("FILE_USER_COUNT: {}", redis_file_count);

    if (DBGetSharePictureCountByUsername(db_conn, user_name, mysq_file_count) <
        0) {
        LogError("DBGetUserFilesCountByUsername failed");
        return -1;
    }

    redis_file_count = (int64_t)mysq_file_count;
    if (CacheSetCount(cache_conn, SHARE_PIC_COUNT + user_name,
                      redis_file_count) < 0) 
    {
        LogError("DBGetUserFilesCountByUsername failed");
        return -1;
    }
    LogInfo("SHARE_PIC_COUNT: {}", redis_file_count);

    return 0;
}

#if API_LOGIN_MUTIL_THREAD
int ApiUserLogin(u_int32_t conn_uuid, std::string &url, std::string &post_data)
{
    UNUSED(url);
    string user_name;
    string pwd;
    string token;
    string str_json;
    // 判断数据是否为空
    if (post_data.empty()) {
        encodeLoginJson(1, token, str_json);
        goto END;
    }
    // 解析json
    if (decodeLoginJson(post_data, user_name, pwd) < 0) {
        LogError("decodeRegisterJson failed");
        encodeLoginJson(1, token, str_json);
        goto END;
    }

    // 验证账号和密码是否匹配
    if (verifyUserPassword(user_name, pwd) < 0) {
        LogError("verifyUserPassword failed");
        encodeLoginJson(1, token, str_json);
        goto END;
    }

    // 生成token
    if (setToken(user_name, token) < 0) {
        LogError("setToken failed");
        encodeLoginJson(1, token, str_json);
        goto END;
    }

    // 加载 我的文件数量  我的分享图片数量

    if (loadMyfilesCountAndSharepictureCount(user_name) < 0) {
        LogError("loadMyfilesCountAndSharepictureCount failed");
        encodeLoginJson(1, token, str_json);
        goto END;
    }
    encodeLoginJson(0, token, str_json);
END:
    char *str_content = new char[HTTP_RESPONSE_HTML_MAX];
    uint32_t ulen = str_json.length();
    snprintf(str_content, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, ulen,
             str_json.c_str());
    str_json = str_content;
    CHttpConn::AddResponseData(conn_uuid, str_json);
    delete str_content;
    
    return 0;
}
#else

int ApiUserLogin(string &url, string &post_data, string &str_json) {
    UNUSED(url);
    string user_name;
    string pwd;
    string token;

    // 判断数据是否为空
    if (post_data.empty()) {
        return -1;
    }
    // 解析json
    if (decodeLoginJson(post_data, user_name, pwd) < 0) {
        LogError("decodeRegisterJson failed");
        encodeLoginJson(1, token, str_json);
        return -1;
    }

    // 验证账号和密码是否匹配
    if (verifyUserPassword(user_name, pwd) < 0) {
        LogError("verifyUserPassword failed");
        encodeLoginJson(1, token, str_json);
        return -1;
    }

    // 生成token
    if (setToken(user_name, token) < 0) {
        LogError("setToken failed");
        encodeLoginJson(1, token, str_json);
        return -1;
    }

    // 加载 我的文件数量  我的分享图片数量

    if (loadMyfilesCountAndSharepictureCount(user_name) < 0) {
        LogError("loadMyfilesCountAndSharepictureCount failed");
        encodeLoginJson(1, token, str_json);
        return -1;
    }

    encodeLoginJson(0, token, str_json);
    return 0;
}
#endif


// 这里用来测试
int ApiUserLoginTest(string &url, string &post_data, string &str_json) {
    UNUSED(url);
    string user_name;
    string pwd;
    string token;

    // 判断数据是否为空
    if (post_data.empty()) {
        return -1;
    }
    // 解析json
    if (decodeLoginJson(post_data, user_name, pwd) < 0) {
        LogError("decodeRegisterJson failed");
        encodeLoginJson(1, token, str_json);
        return -1;
    }

    // 验证账号和密码是否匹配
    if (verifyUserPassword(user_name, pwd) < 0) {
        LogError("verifyUserPassword failed");
        encodeLoginJson(1, token, str_json);
        return -1;
    }

    // 生成token
    if (setToken(user_name, token) < 0) {
        LogError("setToken failed");
        encodeLoginJson(1, token, str_json);
        return -1;
    }

    // 加载 我的文件数量  我的分享图片数量

    if (loadMyfilesCountAndSharepictureCount(user_name) < 0) {
        LogError("loadMyfilesCountAndSharepictureCount failed");
        encodeLoginJson(1, token, str_json);
        return -1;
    }

    encodeLoginJson(0, token, str_json);
    return 0;
}