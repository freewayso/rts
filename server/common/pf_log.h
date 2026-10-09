#pragma once
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace pf {

enum class LogLevel { Debug, Info, Warn, Error };

class Logger {
public:
    static Logger &instance();

    void set_name(const std::string &name);
    void set_file(const std::string &path); /* also mirrors to stderr */
    void log(LogLevel lv, const char *fmt, ...);

private:
    Logger() = default;
    std::mutex mu_;
    std::string name_ = "pf";
    FILE *fp_ = nullptr;
};

} // namespace pf

#define PF_LOG_D(...) ::pf::Logger::instance().log(::pf::LogLevel::Debug, __VA_ARGS__)
#define PF_LOG_I(...) ::pf::Logger::instance().log(::pf::LogLevel::Info, __VA_ARGS__)
#define PF_LOG_W(...) ::pf::Logger::instance().log(::pf::LogLevel::Warn, __VA_ARGS__)
#define PF_LOG_E(...) ::pf::Logger::instance().log(::pf::LogLevel::Error, __VA_ARGS__)
