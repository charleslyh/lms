#pragma once

#include "lms/Foundation.h"

#if defined(__FILE_NAME__)
#define __LMS_FILE__ __FILE_NAME__
#else
#define __LMS_FILE__ __FILE__
#endif

#ifdef _MSC_VER
/* ref: https://docs.microsoft.com/en-us/previous-versions/visualstudio/visual-studio-2010/ms235402(v=vs.100) */
#  define _FORMAT_CHECKER_(arc, fmt_idx, first_to_check)
#else
   /* https://gcc.gnu.org/onlinedocs/gcc-4.0.1/gcc/Function-Attributes.html#Function-Attributes */
#  define _FORMAT_CHECKER_(arc, fmt_idx, first_to_check) __attribute__((format(arc, (fmt_idx), (first_to_check))))
#endif /* _MSVC_VER */

namespace lms {

typedef enum {
  LogLevelVerbose  = 0,
  LogLevelDebug    = 1,
  LogLevelInfo     = 2,
  LogLevelWarning  = 3,
  LogLevelError    = 4,
  LogLevelCritical = 5,
} LogLevel;

/*
 用户自定义日志记录器
 */
class LogWriter {
public:
  virtual void write(const char *log) = 0;
};

void setLogLevel(LogLevel logLevel);
void writeLog(const char *fn, int ln, const char *func, LogLevel lv, const char *fmt, ...) _FORMAT_CHECKER_(printf, 5, 6);

} // namespace lms

#define LMSLog(level, fmt, ...) lms::writeLog(__LMS_FILE__, __LINE__, __FUNCTION__, level, fmt, ##__VA_ARGS__)

#define LMSLogVerbose(fmt, ...)  LMSLog(lms::LogLevelVerbose,  (fmt), ##__VA_ARGS__)
#define LMSLogDebug(fmt, ...)    LMSLog(lms::LogLevelDebug,    (fmt), ##__VA_ARGS__)
#define LMSLogInfo(fmt, ...)     LMSLog(lms::LogLevelInfo,     (fmt), ##__VA_ARGS__)
#define LMSLogWarning(fmt, ...)  LMSLog(lms::LogLevelWarning,  (fmt), ##__VA_ARGS__)
#define LMSLogError(fmt, ...)    LMSLog(lms::LogLevelError,    (fmt), ##__VA_ARGS__)
#define LMSLogCritical(fmt, ...) LMSLog(lms::LogLevelCritical, (fmt), ##__VA_ARGS__)
