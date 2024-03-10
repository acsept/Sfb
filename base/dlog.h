#ifndef LOG_H
#define LOG_H
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/async.h>
#include "spdlog/sinks/stdout_color_sinks.h"
#ifndef SPDLOG_TRACE_ON
#define SPDLOG_TRACE_ON
#endif

#ifndef SPDLOG_DEBUG_ON
#define SPDLOG_DEBUG_ON
#endif

class DLog
{
public:
    static  DLog* GetInstance()
    {
        static DLog dlogger;
        return &dlogger;
    }
    std::shared_ptr<spdlog::logger> getLogger()
	{
		return log_;
	}

    static void SetLevel(char *log_level);
private:
    DLog() {
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(level_);
        consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e][thread %t][%@,%!][%l] : %v");
        sinkList.push_back(consoleSink);
    }

    ~DLog() { }
private:
    std::shared_ptr<spdlog::logger> log_;
    static spdlog::level::level_enum level_;
};

#define LogTrace(...) SPDLOG_LOGGER_CALL(DLog::GetInstance()->getLogger().get(), spdlog::level::trace, __VA_ARGS__)
#define LogDebug(...) SPDLOG_LOGGER_CALL(DLog::GetInstance()->getLogger().get(), spdlog::level::debug, __VA_ARGS__)
#define LogInfo(...) SPDLOG_LOGGER_CALL(DLog::GetInstance()->getLogger().get(), spdlog::level::info, __VA_ARGS__)
#define LogWarn(...) SPDLOG_LOGGER_CALL(DLog::GetInstance()->getLogger().get(), spdlog::level::warn, __VA_ARGS__)
#define LogError(...) SPDLOG_LOGGER_CALL(DLog::GetInstance()->getLogger().get(), spdlog::level::err, __VA_ARGS__)
#define LogCritical(...) SPDLOG_LOGGER_CALL(DLog::GetInstance()->getLogger().get(), spdlog::level::critical, __VA_ARGS__)
#endif
