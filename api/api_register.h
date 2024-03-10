#ifndef _API_REGISTER_H_
#define _API_REGISTER_H_
#include "api_common.h"

int ApiRegisterUser(uint32_t conn_uuid, std::string &url, std::string &post_data);
#endif