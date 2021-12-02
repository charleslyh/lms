#include "Logger.h"
extern "C" {
  #include <SDL2/SDL.h>
}
#include <sys/time.h>

namespace lms {

const int MaxBufferSize = 8192;

class LogWriterConsole : public LogWriter {
public:
  LogWriterConsole() {
    mtx = SDL_CreateMutex();
  }
  
  ~LogWriterConsole() {
    SDL_DestroyMutex(mtx);
  }
  
  void write(const char *log) override {
    SDL_LockMutex(mtx);
    printf("%s", log);
    SDL_UnlockMutex(mtx);
  }

private:
  // printf是非线程安全的，所以需要手工使用一个互斥量来解决串行输出的问题。否则，会导致内容交错，如假设有aaaa,bbbb两行日志
  // 正确输出应该是
  //   aaaa
  //   bbbb
  // 可能的输出
  //   aabbbb
  //   aa
  SDL_mutex *mtx;
};

LogWriter *getLogWriter() {
  static LogWriterConsole consoleLogger;
  return &consoleLogger;
}

static LogLevel _logLevel = LogLevelInfo;

void setLogLevel(LogLevel logLevel) {
  _logLevel = logLevel;
}

static
void writeLogVargs(const char *fn, int ln, const char *func, LogLevel lv, const char *fmt, va_list vargs) {
  constexpr char levelTags[] = {
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

  SDL_threadID threadId = SDL_ThreadID();
  len = snprintf(wptr, remainBufferSz, ".%03u %c/[%lx]/%s:%d/%s", tv.tv_usec / 1000, chLevel, threadId, fn, ln, func);
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

  getLogWriter()->write(buffer);
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

} // namespace lms
