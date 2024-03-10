#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api_common.h"
#include "db_pool.h"
#include <sys/time.h>
#include <time.h>

enum Md5State {
    Md5Ok = 0,
    Md5Failed = 1,
    Md5TokenFaild = 4,
    Md5FileExit = 5,
};


int decodeMd5Json(string &str_json, string &user_name, string &token,
                  string &md5, string &filename) {
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res) {
        LogError("parse md5 json failed ");
        return -1;
    }
    if (root["user"].isNull()) {
        LogError("user null");
        return -1;
    }
    user_name = root["user"].asString();
    if (root["token"].isNull()) {
        LogError("token null");
        return -1;
    }
    token = root["token"].asString();
    if (root["md5"].isNull()) {
        LogError("md5 null");
        return -1;
    }
    md5 = root["md5"].asString();
    if (root["filename"].isNull()) {
        LogError("filename null");
        return -1;
    }
    filename = root["filename"].asString();
    return 0;
}

int encodeMd5Json(int ret, string &str_json) {
    Json::Value root;
    root["code"] = ret;
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}

void handleDealMd5(const char *user, const char *md5, const char *filename,
                   string &str_json) {
    Md5State md5_state = Md5Failed;
    int ret = 0;
    int file_ref_count = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};

    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    sprintf(sql_cmd, "select count from file_info where md5 = '%s'", md5);
    LogInfo("执行: {}", sql_cmd);
    file_ref_count = 0;
    ret = GetResultOneCount(db_conn, sql_cmd, file_ref_count); 
    LogInfo("ret: {}, file_ref_count: {}", ret, file_ref_count);
    if (ret == 0) 
    {
        sprintf(sql_cmd,
                "select * from user_file_list where user = '%s' and md5 = '%s' "
                "and file_name = '%s'",
                user, md5, filename);
        LogInfo("执行: {}", sql_cmd);
        ret = CheckwhetherHaveRecord(db_conn, sql_cmd); 
        if (ret == 1) 
        {
            LogWarn("user: {}->  filename: {}, md5: {}已存在", user, filename, md5);
            md5_state = Md5FileExit; 
            goto END;
        }

        sprintf(sql_cmd, "update file_info set count = %d where md5 = '%s'",
                file_ref_count + 1, md5);
        LogInfo("执行: {}", sql_cmd);
        if (!db_conn->ExecutePassQuery(sql_cmd)) {
            LogError("{} 操作失败", sql_cmd);
            md5_state =
                Md5Failed; 
            goto END;
        }

        struct timeval tv;
        struct tm *ptm;
        char time_str[128];

        gettimeofday(&tv, NULL);
        ptm = localtime(
            &tv.tv_sec); 

        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", ptm);


        sprintf(sql_cmd,
                "insert into user_file_list(user, md5, create_time, file_name, "
                "shared_status, pv) values ('%s', '%s', '%s', '%s', %d, %d)",
                user, md5, time_str, filename, 0, 0);
        LogInfo("执行: {}", sql_cmd);
        if (!db_conn->ExecuteCreate(sql_cmd)) {
            LogError("{} 操作失败", sql_cmd);
            md5_state = Md5Failed;
            sprintf(sql_cmd, "update file_info set count = %d where md5 = '%s'",
                    file_ref_count, md5);
            LogInfo("执行: {}", sql_cmd);
            if (!db_conn->ExecutePassQuery(sql_cmd)) {
                LogError("{} 操作失败", sql_cmd);
            }
            goto END;
        }

        if (CacheIncrCount(cache_conn, FILE_USER_COUNT + string(user)) < 0) {
            LogWarn("CacheIncrCount failed"); 
        }

        md5_state = Md5Ok;
    } else 
    {
        LogInfo("秒传失败");
        md5_state = Md5Failed;
        goto END;
    }

END:
    
    int code = (int)md5_state;
    encodeMd5Json(code, str_json);
}

int ApiMd5(string &url, string &post_data, string &str_json) {
    UNUSED(url);

    string user;
    string md5;
    string token;
    string filename;
    int ret = 0;
    ret = decodeMd5Json(post_data, user, token, md5, filename); //解析json中信息
    if (ret < 0) {
        LogError("decodeMd5Json() err");
        encodeMd5Json((int)Md5Failed, str_json);
        return 0;
    }

    ret = VerifyToken(user, token); //
    if (ret == 0) {
        handleDealMd5(user.c_str(), md5.c_str(), filename.c_str(),
                      str_json); 
        return 0;

    } else {
        LogError("VerifyToken failed");
        encodeMd5Json(HTTP_RESP_TOKEN_ERR, str_json); 
        return 0;
    }
}
