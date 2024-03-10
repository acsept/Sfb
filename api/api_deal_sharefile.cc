#include "api_deal_sharefile.h"

#include "api_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
/// @brief  
/// @param str_json  需要解析的json
/// @param user_name 解析的用户名
/// @param md5       解析的md5值
/// @param filename  解析的文件名
/// @return  成功返回0 失败-1
int decodeDealsharefileJson(string &str_json, string &user_name, string &md5,
                            string &filename) {
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res) {
        LogError("parse reg json failed");
        return -1;
    }

    if (root["user"].isNull()) {
        LogError("user null");
        return -1;
    }
    user_name = root["user"].asString();

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

int encodeDealsharefileJson(int ret, string &str_json) {
    Json::Value root;
    root["code"] = ret;

    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}
//下载量加一
int handlePvFile(string &md5, string &filename) {
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    int ret2 = 0;
    char fileid[1024] = {0};
    int pv = 0;

    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    //文件标示，md5+文件名
    sprintf(fileid, "%s%s", md5.c_str(), filename.c_str());

    sprintf(
        sql_cmd,
        "select pv from share_file_list where md5 = '%s' and file_name = '%s'",
        md5.c_str(), filename.c_str());
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
            "update share_file_list set pv = %d where md5 = '%s' and file_name "
            "= '%s'",
            pv + 1, md5.c_str(), filename.c_str());
    LogInfo("执行: {}", sql_cmd);
    if (!db_conn->ExecuteUpdate(sql_cmd, false)) {
        LogError("{} 操作失败", sql_cmd);
        ret = -1;
        goto END;
    }

    //判断元素是否在集合中(redis操作)
    ret2 = cache_conn->ZsetExit(FILE_PUBLIC_ZSET, fileid);
    if (ret2 == 1) //存在
    {              //如果存在，有序集合score+1
        ret = cache_conn->ZsetIncr(
            FILE_PUBLIC_ZSET,
            fileid); 
        if (ret != 0) {
            LogError("ZsetIncr 操作失败");
        }
    } else if (ret2 == 0) //不存在
    {                     //如果不存在，从mysql导入数据
        //redis集合中增加一个元素(redis操作)
        cache_conn->ZsetAdd(FILE_PUBLIC_ZSET, pv + 1, fileid);

        //redis对应的hash也需要变化 (redis操作)
        //     fileid ------>  filename
        cache_conn->Hset(FILE_NAME_HASH, fileid, filename);
    } else //出错
    {
        ret = -1;
        goto END;
    }

END:

    if (ret == 0) {
        return HTTP_RESP_OK;
    } else {
        return HTTP_RESP_FAIL;
    }
}

//取消分享文件的数据库操作
int handleCancelShareFile(string &user_name, string &md5, string &filename) {
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    char fileid[1024] = {0};
    int ret2;

    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    //文件标示，md5+文件名
    sprintf(fileid, "%s%s", md5.c_str(), filename.c_str());

    // 共享状态机设置为0
    sprintf(sql_cmd,
            "update user_file_list set shared_status = 0 where user = '%s' and "
            "md5 = '%s' and file_name = '%s'",
            user_name.c_str(), md5.c_str(), filename.c_str());
    LogInfo("执行: {}", sql_cmd);
    if (!db_conn->ExecuteUpdate(sql_cmd, false)) {
        LogError("{} 操作失败", sql_cmd);
        ret = -1;
        goto END;
    }

    // redis中共享文件数量-1
    ret2 = CacheDecrCount(cache_conn, FILE_PUBLIC_COUNT);
    if (ret2 < 0) {
        LogError("{} 操作失败", sql_cmd);
        ret = -1;
        goto END;
    }

    //删除在共享列表的数据
    sprintf(sql_cmd,
            "delete from share_file_list where user = '%s' and md5 = '%s' and "
            "file_name = '%s'",
            user_name.c_str(), md5.c_str(), filename.c_str());
    LogInfo("执行: {}, ret = {}", sql_cmd, ret);
    
    if (!db_conn->ExecuteDrop(sql_cmd)) {
        LogError("{} 操作失败", sql_cmd);
        ret = -1;
        goto END;
    }

    //redis记录操作
    //有序集合删除指定成员
    ret = cache_conn->ZsetZrem(FILE_PUBLIC_ZSET, fileid);
    if (ret != 0) {
        LogInfo("执行: ZsetZrem 操作失败");
        goto END;
    }

    //从hash移除相应记录
    LogInfo("Hdel FILE_NAME_HASH  {}", fileid);
    ret = cache_conn->Hdel(FILE_NAME_HASH, fileid);
    if (ret < 0) {
        LogInfo("执行: hdel 操作失败: ret = {}", ret);
        goto END;
    }

END:
    if (ret == 0) {
        return (HTTP_RESP_OK);
    } else {
        return (HTTP_RESP_FAIL);
    }
}
//存储一个文件的数据库操作
int handleSaveFile(string &user_name, string &md5, string &filename) {
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    int ret2 = 0;
    struct timeval tv;
    struct tm *ptm;
    char time_str[128];
    int count;
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);
    CacheManager *cache_manager = CacheManager::getInstance();
    CacheConn *cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    //查看此用户，文件名和md5是否存在，如果存在说明此文件存在
    sprintf(sql_cmd,
            "select * from user_file_list where user = '%s' and md5 = '%s' and "
            "file_name = '%s'",
            user_name.c_str(), md5.c_str(), filename.c_str());
    //返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败

    // 有记录返回1，错误返回-1，无记录返回0
    ret2 = CheckwhetherHaveRecord(db_conn, sql_cmd);
    if (ret2 == 1) { //如果有结果，说明此用户已有此文件
        LogError("user_name: {}, filename: {}, md5: {} 已存在", user_name, filename, md5);
        ret = -2; //返回-2错误码
        goto END;
    }
    if (ret2 < 0) {
        LogError("{} 操作失败", sql_cmd);
        ret = -1; //返回-1错误码
        goto END;
    }

    //文件信息表，查找该文件的计数器
    sprintf(sql_cmd, "select count from file_info where md5 = '%s'", md5.c_str());
    count = 0;
    ret2 = GetResultOneCount(db_conn, sql_cmd, count); //执行sql语句
    if (ret2 != 0) {
        LogError("{} 操作失败", sql_cmd);
        ret = -1;
        goto END;
    }

    sprintf(sql_cmd, "update file_info set count = %d where md5 = '%s'",
            count + 1, md5.c_str());
    if (!db_conn->ExecuteUpdate(sql_cmd)) {
        LogError("{} 操作失败", sql_cmd);
        ret = -1;
        goto END;
    }

    // 更新用户所有的文件

    //使用函数gettimeofday()函数来得到时间。它的精度可以达到微妙
    gettimeofday(&tv, NULL);
    ptm = localtime(
        &tv.tv_sec); 
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", ptm);

    sprintf(sql_cmd,
            "insert into user_file_list(user, md5, create_time, file_name, "
            "shared_status, pv) values ('%s', '%s', '%s', '%s', %d, %d)",
            user_name.c_str(), md5.c_str(), time_str, filename.c_str(), 0, 0);
    if (!db_conn->ExecuteCreate(sql_cmd)) {
        LogError("{} 操作失败", sql_cmd);
        ret = -1;
        goto END;
    }

    // 查询用户文件数量，更新该字段
    if (CacheIncrCount(cache_conn, FILE_USER_COUNT + user_name) < 0) {
        LogError("CacheIncrCount 操作失败");
        ret = -1;
        goto END;
    }
    ret = 0;
END:

    if (ret == 0) {
        return (HTTP_RESP_OK);
    } else if (ret == -1) {
        return (HTTP_RESP_FAIL);
    } else if (ret == -2) {
        return (HTTP_RESP_FILE_EXIST);
    }
    return 0;
}

/// @brief           解析url和json进行相应操作
/// @param url       需要解析的url
/// @param post_data 需要解析的json
/// @param str_json  错误返回的json
/// @return          失败返回0
int ApiDealsharefile(string &url, string &post_data, string &str_json) {
    char cmd[20];
    string user_name;
    string token;
    string md5;      //文件md5码
    string filename; //文件名字
    int ret = 0;

    //解析命令
    QueryParseKeyValue(url.c_str(), "cmd", cmd, NULL);

    ret = decodeDealsharefileJson(post_data, user_name, md5, filename);
    LogInfo("cmd: {}, user_name: {}, md5: {}, filename: {}", cmd, user_name, md5, filename);
    if (ret != 0) {
        encodeDealsharefileJson(HTTP_RESP_FAIL, str_json);
        return 0;
    }
    ret = 0;
    if (strcmp(cmd, "cancel") == 0) 
    {
        ret = handleCancelShareFile(user_name, md5, filename);
    } else if (strcmp(cmd, "save") == 0) 
    {
        ret = handleSaveFile(user_name, md5, filename);
    } else if (strcmp(cmd, "pv") == 0) 
    {
        ret = handlePvFile(md5, filename);
    }

    if (ret < 0)
        encodeDealsharefileJson(HTTP_RESP_FAIL, str_json);
    else
        encodeDealsharefileJson(HTTP_RESP_OK, str_json);

    return 0;
}