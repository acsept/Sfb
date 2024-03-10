
#include "api_common.h"
#include "api_dealfile.h"
#include "api_upload.h"
#include "config_file_reader.h"
#include "http_conn.h"
#include "netlib.h"
#include "util.h"
#include "dlog.h"
 
void http_callback(void *callback_data, uint8_t msg, uint32_t handle,
                   void *pParam) {
    UNUSED(callback_data);
    UNUSED(pParam);
    if (msg == NETLIB_MSG_CONNECT) {
        CHttpConn *pConn = new CHttpConn();
        pConn->OnConnect(handle);
    } else {
        LogError("!!!error msg:{}", msg);
    }
}

void http_loop_callback(void *callback_data, uint8_t msg, uint32_t handle,
                        void *pParam) {
    UNUSED(callback_data);
    UNUSED(msg);
    UNUSED(handle);
    UNUSED(pParam);
    CHttpConn::SendResponseDataList(); // 静态函数, 将要发送的数据循环发给客户端
}

int initHttpConn(uint32_t thread_num) {
    g_thread_pool.Init(thread_num); // 初始化线程数量
    g_thread_pool.Start();          // 启动多线程
    netlib_add_loop(http_loop_callback,NULL); // http_loop_callback被epoll所在线程循环调用
    return 0;
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN); //忽略SIGPIPE信号
    int ret = 0;
    char *str_tc_http_server_conf = NULL;
    if(argc > 1) {
        str_tc_http_server_conf = argv[1];  // 指向配置文件路径
    } else {
        str_tc_http_server_conf = (char *)"tc_http_server.conf";
    }

    printf("conf file path: %s\n", str_tc_http_server_conf);
    
     // 读取配置文件
    CConfigFileReader config_file(str_tc_http_server_conf);     //读取配置文件
    char *log_level = config_file.GetConfigName("log_level");   //读取日志设置级别
    DLog::SetLevel(log_level);   //设置日志打印级别
    // 短链主要是将图片链接转成短链
    char *str_enable_shorturl = config_file.GetConfigName("enable_shorturl");
    uint16_t enable_shorturl = atoi(str_enable_shorturl);   //1开启短链，0不开启短链
    char *shorturl_server_address = config_file.GetConfigName("shorturl_server_address");// 短链服务地址
    char *shorturl_server_access_token = config_file.GetConfigName("shorturl_server_access_token");

    char *http_listen_ip = config_file.GetConfigName("HttpListenIP");
    char *str_http_port = config_file.GetConfigName("HttpPort");        //8081 -- nginx.conf,当前服务的端口
    char *dfs_path_client = config_file.GetConfigName("dfs_path_client"); // /etc/fdfs/client.conf
    char *storage_web_server_ip = config_file.GetConfigName("storage_web_server_ip"); //后续可以配置域名
    char *storage_web_server_port = config_file.GetConfigName("storage_web_server_port");

    char *str_thread_num = config_file.GetConfigName("ThreadNum");  //线程池数量，目前是epoll + 线程池方式
    uint32_t thread_num = atoi(str_thread_num);

    LogInfo("main into"); //单例模式 日志库 spdlog
    LogInfo("main into2 {}", "darren"); // 只是为了测试日志


    // 初始化mysql、redis连接池，内部也会读取读取配置文件tc_http_server.conf
    CacheManager::SetConfPath(str_tc_http_server_conf); //设置配置文件路径
    CacheManager *cache_manager = CacheManager::getInstance();
    if (!cache_manager) {
        LogError("CacheManager init failed");
        return -1;
    }

    CDBManager::SetConfPath(str_tc_http_server_conf);   //设置配置文件路径
    CDBManager *db_manager = CDBManager::getInstance();
    if (!db_manager) {
        LogError("DBManager init failed");
        return -1;
    }


    // 将配置文件的参数传递给对应模块
    if(enable_shorturl == 1) {
        ApiUploadInit(dfs_path_client, storage_web_server_ip, storage_web_server_port, shorturl_server_address,
        shorturl_server_access_token);
    } else {
        ApiUploadInit(dfs_path_client, storage_web_server_ip, storage_web_server_port, "", "");
    }
    
    ret = ApiDealfileInit(dfs_path_client);

    if (ApiInit() < 0) {
        LogError("ApiInit failed");
        return -1;
    }

    // 检测监听ip和端口
    if (!http_listen_ip || !str_http_port) {
        LogError("config item missing, exit... ip:{}, port:{}", http_listen_ip,
               str_http_port);
        return -1;
    }
    //reactor网络模型 1epoll+ 线程池
    ret = netlib_init();
    if (ret == NETLIB_ERROR) {
        LogError("netlib_init failed");
        return ret;
    }

    uint16_t http_port = atoi(str_http_port);
    CStrExplode http_listen_ip_list(http_listen_ip, ';');
    for (uint32_t i = 0; i < http_listen_ip_list.GetItemCnt(); i++) {
        ret = netlib_listen(http_listen_ip_list.GetItem(i), http_port,
                            http_callback, NULL);
        if (ret == NETLIB_ERROR)
            return ret;
    }
    initHttpConn(thread_num);

    LogInfo("server start listen on:For http://{}:{}", http_listen_ip, http_port);

    LogInfo("now enter the event loop...");

    WritePid();
    // 超时参数影响回发客户端的时间
    netlib_eventloop(1);

    return 0;
}
