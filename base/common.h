#ifndef _TC_COMMON_H_
#define _TC_COMMON_H_


#include <string>
using std::string;

#include "cache_pool.h"
#include "db_pool.h"
#include "dlog.h"
#include "json/json.h"

#define FILE_NAME_LEN (256)    //文件名字长度
#define TEMP_BUF_MAX_LEN (512) //临时缓冲区大小
#define FILE_URL_LEN (512)     //文件所存放storage的host_name长度
#define HOST_NAME_LEN (30)     //主机ip地址长度
#define USER_NAME_LEN (128)    //用户名字长度
#define TOKEN_LEN (128)        //登陆token长度
#define MD5_LEN (256)          //文件md5长度
#define PWD_LEN (256)          //密码长度
#define TIME_STRING_LEN (25)   //时间戳长度
#define SUFFIX_LEN (8)         //后缀名长度
#define PIC_NAME_LEN (10)      //图片资源名字长度
#define PIC_URL_LEN (256)      //图片资源url名字长度

#define HTTP_RESP_OK 0
#define HTTP_RESP_FAIL 1           //
#define HTTP_RESP_USER_EXIST 2     // 用户存在
#define HTTP_RESP_DEALFILE_EXIST 3 // 别人已经分享此文件
#define HTTP_RESP_TOKEN_ERR 4      //  token验证失败
#define HTTP_RESP_FILE_EXIST 5     //个人已经存储了该文件

#define UNUSED(expr)                                                           \
    do {                                                                       \
        (void)(expr);                                                          \
    } while (0)

int TrimSpace(char *inbuf);
int QueryParseKeyValue(const char *query, const char *key, char *value,
                       int *value_len_p);
int GetFileSuffix(const char *file_name, char *suffix);

int VerifyToken(char *user, char *token);

template <typename... Args>
std::string FormatString(const std::string &format, Args... args);

string RandomString(const int len);
int VerifyToken(string &user_name, string &token);

#define SQL_MAX_LEN (512) // sql语句长度

int GetResultOneCount(CDBConn *db_conn, char *sql_cmd, int &count);
int GetResultOneStatus(CDBConn *db_conn, char *sql_cmd, int &shared_status);

int CheckwhetherHaveRecord(CDBConn *db_conn, char *sql_cmd);

int GetUserFilesCountByUsername(string user_name, int &count);

int GetShareFilesCount(int &count);




#endif