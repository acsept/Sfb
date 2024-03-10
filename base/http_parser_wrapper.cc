#include "http_parser_wrapper.h"
#include "http_parser.h"

#define MAX_REFERER_LEN 32

CHttpParserWrapper::CHttpParserWrapper() {}

oid CHttpParserWrapper::ParseHttpContent(const char *buf, uint32_t len) {
    http_parser_init(&http_parser_, HTTP_REQUEST);
    memset(&settings_, 0, sizeof(settings_));
    settings_.on_url = OnUrl;
    settings_.on_header_field = OnHeaderField;
    settings_.on_header_value = OnHeaderValue;
    settings_.on_headers_complete = OnHeadersComplete;
    settings_.on_body = OnBody;
    settings_.on_message_complete = OnMessageComplete;
    settings_.object = this;

    read_all_ = false;
    read_referer_ = false;
    read_forward_ip_ = false;
    read_user_agent_ = false;
    read_content_type_ = false;
    read_content_len_ = false;
    read_host_ = false;
    total_length_ = 0;
    url_.clear();
    body_content_.clear();
    referer_.clear();
    forward_ip_.clear();
    user_agent_.clear();
    content_type_.clear();
    content_len_ = 0;
    host_.clear();

    http_parser_execute(&http_parser_, &settings_, buf, len);
}

int CHttpParserWrapper::OnUrl(http_parser *parser, const char *at,
                              size_t length, void *obj) {
    ((CHttpParserWrapper *)obj)->SetUrl(at, length);
    return 0;
}

int CHttpParserWrapper::OnHeaderField(http_parser *parser, const char *at,
                                      size_t length, void *obj) {
    if (!((CHttpParserWrapper *)obj)->HasReadReferer()) {
        if (strncasecmp(at, "Referer", 7) == 0) {
            ((CHttpParserWrapper *)obj)->SetReadReferer(true);
        }
    }

    if (!((CHttpParserWrapper *)obj)->HasReadForwardIP()) {
        if (strncasecmp(at, "X-Forwarded-For", 15) == 0) {
            ((CHttpParserWrapper *)obj)->SetReadForwardIP(true);
        }
    }

    if (!((CHttpParserWrapper *)obj)->HasReadUserAgent()) {
        if (strncasecmp(at, "User-Agent", 10) == 0) {
            ((CHttpParserWrapper *)obj)->SetReadUserAgent(true);
        }
    }

    if (!((CHttpParserWrapper *)obj)->HasReadContentType()) {
        if (strncasecmp(at, "Content-Type", 12) == 0) {
            ((CHttpParserWrapper *)obj)->SetReadContentType(true);
        }
    }

    if (!((CHttpParserWrapper *)obj)->HasReadContentLen()) {
        if (strncasecmp(at, "Content-Length", 14) == 0) {
            ((CHttpParserWrapper *)obj)->SetReadContentLen(true);
        }
    }
    if (!((CHttpParserWrapper *)obj)->HasReadHost()) {
        if (strncasecmp(at, "Host", 4) == 0) {
            ((CHttpParserWrapper *)obj)->SetReadHost(true);
        }
    }
    return 0;
}

