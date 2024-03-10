#include "api_dealfile.h"
#include "api_common.h"
#include "redis_keys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

enum ShareState {
    ShareOk = 0,        // 分享成功
    ShareFail = 1,      // 分享失败
    ShareHad = 3,       // 别人已经分享此文件
    ShareTokenFail = 4, // token验证失败
};

int decodeDealfileJson(string &str_json, string &user_name, string &token,
                       string &md5, string &filename) {
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res) {
        LogError("parse reg json failed ");
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

int encodeDealfileJson(int ret, string &str_json) {
    Json::Value root;
    root["code"] = ret;
    Json::FastWriter writer;
    str_json = writer.write(root);

    LogInfo("str_json: {}", str_json);
    return 0;
}

//
int handleShareFile(string &user, string &md5, string &filename) {
    ShareState share_state = ShareFail;
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    char fileid[1024] = {0};
    int ret2 = 0;
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);

    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);
    sprintf(fileid, "%s%s", md5.c_str(), filename.c_str());
    if (cache_conn) {
        ret2 = cache_conn->ZsetExit(FILE_PUBLIC_ZSET, fileid);
    } else {
        ret2 = 0;
    }
    LogInfo("fileid: {}, ZsetExit: {}", fileid, ret2);
    if (ret2 == 1) //存在
    {
        LogWarn("别人已经分享此文件");
        share_state = ShareHad;
        goto END;
    } else if (ret2 == 0) //不存在
    {
        sprintf(sql_cmd,
                "select * from share_file_list where md5 = '%s' and file_name "
                "= '%s'",
                md5.c_str(), filename.c_str());
        ret2 = CheckwhetherHaveRecord(
            db_conn,
            sql_cmd);
            if (ret2 == 1) 
        {
            cache_conn->ZsetAdd(FILE_PUBLIC_ZSET, 0, fileid);
            cache_conn->Hset(FILE_NAME_HASH, fileid, filename);
            LogWarn("别人已经分享此文件");
            share_state = ShareHad;
            goto END;
        }
    } else //出错
    {
        ret = -1;
        goto END;
    }
    sprintf(sql_cmd,
            "update user_file_list set shared_status = 1 where user = '%s' and "
            "md5 = '%s' and file_name = '%s'",
            user.c_str(), md5.c_str(), filename.c_str());

    if (!db_conn->ExecuteUpdate(sql_cmd, false)) {
        LogError("{} 操作失败", sql_cmd);
        ret = -1;
        goto END;
    }

    time_t now;
    ;
    char create_time[TIME_STRING_LEN];
    //获取当前时间
    now = time(NULL);
    strftime(create_time, TIME_STRING_LEN - 1, "%Y-%m-%d %H:%M:%S",
             localtime(&now));
    sprintf(sql_cmd,
            "insert into share_file_list (user, md5, create_time, file_name, "
            "pv) values ('%s', '%s', '%s', '%s', %d)",
            user.c_str(), md5.c_str(), create_time, filename.c_str(), 0);
    if (!db_conn->ExecuteCreate(sql_cmd)) {
        LogError("{} 操作失败", sql_cmd);
        ret = -1;
        goto END;
    }

    // 共享文件数量+1
    ret = CacheIncrCount(cache_conn, FILE_PUBLIC_COUNT);
    if (ret < 0) {
        LogError("CacheIncrCount failed");
        ret = -1;
        goto END;
    }
    cache_conn->ZsetAdd(FILE_PUBLIC_ZSET, 0,
                        fileid); 
    LogInfo("Hset FILE_NAME_HASH {}-{}", fileid, filename);
    ret = cache_conn->Hset(FILE_NAME_HASH, fileid, filename);
    if (ret < 0) {
        LogWarn("Hset FILE_NAME_HASH failed");
    }
    share_state = ShareOk;
END:
    return (int)share_state;
}

int handleDeleteFile(string &user, string &md5, string &filename) {
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    char fileid[1024] = {0};
    int ret2 = 0;
    int count = 0;
    int is_shared = 0;        
    int redis_has_record = 0; //标志redis是否有记录

    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);

    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    sprintf(fileid, "%s%s", md5.c_str(), filename.c_str());

    //判断此文件是否已经分享，判断集合有没有这个文件，如果有，说明别人已经分享此文件
    ret2 = cache_conn->ZsetExit(FILE_PUBLIC_ZSET, fileid);
    LogInfo("ret2: {}", ret2);
    if (ret2 == 1) //存在
    {
        is_shared = 1;        
        redis_has_record = 1; 
    } else if (ret2 == 0)     //不存在
    { //如果集合没有此元素，可能因为redis中没有记录，再从mysql中查询，如果mysql也没有，说明真没有(mysql操作)

        is_shared = 0;
        sprintf(sql_cmd,
                "select shared_status from user_file_list where user = '%s' "
                "and md5 = '%s' and file_name = '%s'",
                user.c_str(), md5.c_str(), filename.c_str());
        LogInfo("执行: {}", sql_cmd);
        int shared_status = 0;
        ret2 =
            GetResultOneStatus(db_conn, sql_cmd, shared_status);
        if (ret2 == 0) {
            LogInfo("GetResultOneCount share  = {}", shared_status);
            is_shared = shared_status;
        }
    } else //出错
    {
        ret = -1;
        goto END;
    }
    LogInfo("is_shared = {}", is_shared);
    if (is_shared == 1) {
        sprintf(sql_cmd,
                "delete from share_file_list where user = '%s' and md5 = '%s' "
                "and file_name = '%s'",
                user.c_str(), md5.c_str(), filename.c_str());
        LogInfo("执行: {}", sql_cmd);
        if (!db_conn->ExecuteDrop(sql_cmd)) {
            LogError("{} 操作失败", sql_cmd);
            ret = -1;
            goto END;
        }

        if (CacheDecrCount(cache_conn, FILE_PUBLIC_COUNT) < 0) {
            LogError("CacheDecrCount 操作失败");
            ret = -1;
            goto END;
        }

        //如果redis有记录，redis需要处理，删除相关记录
        if (1 == redis_has_record) {
            //有序集合删除指定成员
            cache_conn->ZsetZrem(FILE_PUBLIC_ZSET, fileid);

            //从hash移除相应记录
            cache_conn->Hdel(FILE_NAME_HASH, fileid);
        }
    }

    //用户文件数量-1
    if (CacheDecrCount(cache_conn, FILE_USER_COUNT + user) < 0) {
        LogError("CacheDecrCount 操作失败");
        ret = -1;
        goto END;
    }

    //删除用户文件列表数据
    sprintf(sql_cmd,
            "delete from user_file_list where user = '%s' and md5 = '%s' and "
            "file_name = '%s'",
            user.c_str(), md5.c_str(), filename.c_str());
    LogInfo("执行: {}", sql_cmd);
    if (!db_conn->ExecuteDrop(sql_cmd)) {
        LogError("{} 操作失败", sql_cmd);
        ret = -1;
        goto END;
    }

    sprintf(sql_cmd, "select count from file_info where md5 = '%s'",
            md5.c_str());
    LogInfo("执行: {}", sql_cmd);
    count = 0;
    ret2 = GetResultOneCount(db_conn, sql_cmd, count);
    LogInfo("ret2: {}, count: {}", ret2, count);
    if (ret2 != 0) {
        LogError("{} 操作失败", sql_cmd);
        ret = -1;
        goto END;
    }

    if (count > 0) {
        count -= 1;
        sprintf(sql_cmd, "update file_info set count=%d where md5 = '%s'",
                count, md5.c_str());
        LogInfo("执行: {}", sql_cmd);
        if (!db_conn->ExecuteUpdate(sql_cmd)) {
            LogError("{} 操作失败", sql_cmd);
            ret = -1;
            goto END;
        }
    }

    if (count == 0) 
    {
        sprintf(sql_cmd, "select file_id from file_info where md5 = '%s'",
                md5.c_str());
        string fileid;
        CResultSet *result_set = db_conn->ExecuteQuery(sql_cmd);
        if (result_set->Next()) {
            fileid = result_set->GetString("file_id");
        }
        sprintf(sql_cmd, "delete from file_info where md5 = '%s'", md5.c_str());
        if (!db_conn->ExecuteDrop(sql_cmd)) {
            LogWarn("{} 操作失败", sql_cmd);
        }
        ret2 = RemoveFileFromFastDfs(fileid.c_str());
        if (ret2 != 0) {
            LogInfo("RemoveFileFromFastDfs err: {}", ret2);
            ret = -1;
            goto END;
        }
    }
    ret = 0;
END:
    if (ret == 0) {
        return HTTP_RESP_OK;
    } else {
        return (HTTP_RESP_FAIL);
    }
}

//下载标志+1
int handlePvFile(string &user, string &md5, string &filename) {
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    int pv = 0;

    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);

    sprintf(sql_cmd,
            "select pv from user_file_list where user = '%s' and md5 = '%s' "
            "and file_name = '%s'",
            user.c_str(), md5.c_str(), filename.c_str());

    LogInfo("执行: {}", sql_cmd);
    CResultSet *result_set = db_conn->ExecuteQuery(sql_cmd);
    if (result_set && result_set->Next()) {
        pv = result_set->GetInt("pv");
    } else {
        LogError("{} 操作失败", sql_cmd);
        ret = -1;
        goto END;
    }

    sprintf(sql_cmd,
            "update user_file_list set pv = %d where user = '%s' and md5 = "
            "'%s' and file_name = '%s'",
            pv + 1, user.c_str(), md5.c_str(), filename.c_str());
    LogInfo("执行: {}", sql_cmd);
    if (!db_conn->ExecuteUpdate(sql_cmd)) {
        LogError("{} 操作失败", sql_cmd);
        ret = -1;
        goto END;
    }

END:

    if (ret == 0) {
        return (HTTP_RESP_OK);
    } else {
        return (HTTP_RESP_FAIL);
    }
}

int ApiDealfileInit(char *dfs_path_client) {
    s_dfs_path_client = dfs_path_client;
    return 0;
}

int ApiDealfile(string &url, string &post_data, string &str_json) {
    char cmd[20];
    string user_name;
    string token;
    string md5;      
    string filename; 
    int ret = 0;

    QueryParseKeyValue(url.c_str(), "cmd", cmd, NULL);
    LogInfo("cmd = {}", cmd);

    ret = decodeDealfileJson(post_data, user_name, token, md5,
                             filename); //解析json信息
    if (ret < 0) {
        encodeDealfileJson(ShareFail, str_json); // token验证失败错误码
        return 0;
    }

    LogInfo("user_name: {}, token: {}, md5: {}, filename: {}", user_name, token,md5, filename);
    ret = VerifyToken(user_name, token);
    if (ret != 0) {
        encodeDealfileJson(ShareTokenFail, str_json); // token验证失败错误码
        return 0;
    }

    if (strcmp(cmd, "share") == 0) //分享文件
    {
        ret = handleShareFile(user_name, md5, filename);
        encodeDealfileJson(ret, str_json);
    } else if (strcmp(cmd, "del") == 0) //删除文件
    {
        ret = handleDeleteFile(user_name, md5, filename);
        encodeDealfileJson(ret, str_json);
    } else if (strcmp(cmd, "pv") == 0) //文件下载标志处理
    {
        ret = handlePvFile(user_name, md5, filename);
        encodeDealfileJson(ret, str_json);
    } else {
        encodeDealfileJson(1, str_json);
    }

    return 0;
}