#include "pf_log.h"
#include <chrono>
#include <ctime>
#include <cstring>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace pf {

Logger &Logger::instance()
{
    static Logger L;
    return L;
}

void Logger::set_name(const std::string &name)
{
    std::lock_guard<std::mutex> lk(mu_);
    name_ = name;
}

void Logger::set_file(const std::string &path)
{
    std::lock_guard<std::mutex> lk(mu_);
    if(fp_) {
        fclose(fp_);
        fp_ = nullptr;
    }
#ifdef _WIN32
    _mkdir("logs");
#else
    mkdir("logs", 0755);
#endif
    fp_ = fopen(path.c_str(), "a");
    if(!fp_)
        fprintf(stderr, "[log] cannot open %s\n", path.c_str());
}

void Logger::log(LogLevel lv, const char *fmt, ...)
{
    const char *ls = "I";
    switch(lv) {
    case LogLevel::Debug: ls = "D"; break;
    case LogLevel::Info:  ls = "I"; break;
    case LogLevel::Warn:  ls = "W"; break;
    case LogLevel::Error: ls = "E"; break;
    }

    using clock = std::chrono::system_clock;
    auto now = clock::now();
    std::time_t t = clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char tstr[64];
    std::snprintf(tstr, sizeof(tstr), "%02d:%02d:%02d.%03d",
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, (int)ms.count());

    char body[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    char line[2300];
    std::snprintf(line, sizeof(line), "%s [%s][%s] %s\n", tstr, ls, name_.c_str(), body);

    std::lock_guard<std::mutex> lk(mu_);
    fputs(line, stderr);
    fflush(stderr);
    if(fp_) {
        fputs(line, fp_);
        fflush(fp_);
    }
}

} // namespace pf
