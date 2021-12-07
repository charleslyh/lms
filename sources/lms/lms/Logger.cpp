#include "Logger.h"
#include "Module.h"
#include <sys/time.h>
#include <thread>
#include <sstream>

namespace lms {

const int MaxBufferSize = 8192;

static LogWriter *_logWriter;
static LogLevel _logLevel = LogLevelInfo;

void setLogLevel(LogLevel logLevel) {
  _logLevel = logLevel;
}

static
void writeLogVargs(const char *fn, int ln, const char *func, LogLevel lv, const char *fmt, va_list vargs) {
  static constexpr char levelTags[] = {
    [LogLevelVerbose]  = 'V',
    [LogLevelDebug]    = 'D',
    [LogLevelInfo]     = 'I',
    [LogLevelWarning]  = 'W',
    [LogLevelError]    = 'E',
    [LogLevelCritical] = 'C',
  };
  
  char chLevel = levelTags[lv];
  
  char buffer[MaxBufferSize];
  char *wptr = buffer;
  int remainBufferSz = MaxBufferSize;

  // https://www.delftstack.com/howto/cpp/how-to-get-time-in-milliseconds-cpp/
  struct timeval tv{};
  gettimeofday(&tv, nullptr);
  
  struct tm info;
  localtime_r(&tv.tv_sec, &info);
  int len = (int)strftime(wptr, remainBufferSz, "%Y-%m-%d %H:%M:%S", &info);
  wptr += len;
  remainBufferSz -= len;

  std::thread::id this_id = std::this_thread::get_id();
  std::stringstream ss;
  ss << std::this_thread::get_id();
  
  len = snprintf(wptr, remainBufferSz, ".%03u %c/[%s]/%s:%d/%s", tv.tv_usec / 1000, chLevel, ss.str().c_str(), fn, ln, func);
  wptr += len;
  remainBufferSz -= len;
  
  if (fmt != nullptr) {
    len = snprintf(wptr, remainBufferSz, " | ");
    wptr += len;
    remainBufferSz -= len;

    len = vsnprintf(wptr, remainBufferSz, fmt, vargs);
    wptr += len;
    remainBufferSz -= len;
  }

  snprintf(wptr, remainBufferSz, "\n");

  _logWriter->write(buffer);
}

void writeLog(const char *fname, int line, const char *func, LogLevel level, const char *fmt, ...) {
  if (level < _logLevel) {
    return;
  }

  va_list vargs;
  va_start(vargs, fmt);
  writeLogVargs(fname, line, func, level, fmt, vargs);
  va_end(vargs);
}

// 插件需要实现的符号
extern LogWriter* createLogWriter();

static void setupLoggerConsole() {
  _logWriter = createLogWriter();
}

static void teardownLoggerConsole() {
  delete _logWriter;
  _logWriter = nullptr;
}

Module moduleLogger = {
  .name     = "Logger",
  .setup    = setupLoggerConsole,
  .teardown = teardownLoggerConsole
};

} // namespace lms
