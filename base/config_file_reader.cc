
#include "config_file_reader.h"

CConfigFileReader::CConfigFileReader(const char *filename) {
    _LoadFile(filename);
}

CConfigFileReader::~CConfigFileReader() {}
//根据name找对应的值 配置文件中
char *CConfigFileReader::GetConfigName(const char *name) {
    if (!load_ok_)
        return NULL;

    char *value = NULL;
    map<string, string>::iterator it = config_map_.find(name);
    if (it != config_map_.end()) {
        value = (char *)it->second.c_str();
    }

    return value;
}

int CConfigFileReader::SetConfigValue(const char *name, const char *value) {
    if (!load_ok_)
        return -1;

    map<string, string>::iterator it = config_map_.find(name);
    if (it != config_map_.end()) {
        it->second = value;
    } else {
        config_map_.insert(make_pair(name, value));
    }
    return _WriteFIle();
}

//一行一行解析文件加入map
void CConfigFileReader::_LoadFile(const char *filename) {
    config_file_.clear();
    config_file_.append(filename);
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("can not open %s,errno = %d", filename, errno);
        return;
    }

    char buf[256];
    for (;;) {
        char *p = fgets(buf, 256, fp);
        if (!p)
            break;

        size_t len = strlen(buf);
        if (buf[len - 1] == '\n')
            buf[len - 1] = 0; 

        char *ch = strchr(buf, '#');
        if (ch)
            *ch = 0;

        if (strlen(buf) == 0)
            continue;

        _ParseLine(buf);
    }

    fclose(fp);
    load_ok_ = true;
}

//将map中的对写入文件中
int CConfigFileReader::_WriteFIle(const char *filename) {
    FILE *fp = NULL;
    if (filename == NULL) {
        fp = fopen(config_file_.c_str(), "w");
    } else {
        fp = fopen(filename, "w");
    }
    if (fp == NULL) {
        return -1;
    }

    char szPaire[128];
    map<string, string>::iterator it = config_map_.begin();
    for (; it != config_map_.end(); it++) {
        memset(szPaire, 0, sizeof(szPaire));
        snprintf(szPaire, sizeof(szPaire), "%s=%s\n", it->first.c_str(),
                 it->second.c_str());
        uint32_t ret = fwrite(szPaire, strlen(szPaire), 1, fp);
        if (ret != 1) {
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    return 0;
}
//解析一行的数据写入map
void CConfigFileReader::_ParseLine(char *line) {
    char *p = strchr(line, '=');
    if (p == NULL)
        return;

    *p = 0;
    char *key = _TrimSpace(line);
    char *value = _TrimSpace(p + 1);
    if (key && value) {
        config_map_.insert(make_pair(key, value));
    }
}

//去掉空格
char *CConfigFileReader::_TrimSpace(char *name) {
    char *start_pos = name;
    while ((*start_pos == ' ') || (*start_pos == '\t')) {
        start_pos++;
    }

    if (strlen(start_pos) == 0)
        return NULL;

    char *end_pos = name + strlen(name) - 1;
    while ((*end_pos == ' ') || (*end_pos == '\t')) {
        *end_pos = 0;
        end_pos--;
    }

    int len = (int)(end_pos - start_pos) + 1;
    if (len <= 0)
        return NULL;

    return start_pos;
}
