#include "LMSFoundation/Logger.h"
extern "C" {
  #include <SDL2/SDL.h>
}
#include <time.h>

namespace lms {

const int MaxBufferSize = 8192;

class LogWriterConsole : public LogWriter {
public:
  void write(const char *log) override {
    printf("%s", log);
  }  
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
  const char levelTags[] = {
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

  time_t ts;
  struct tm *info;
  char strDatetime[80];
  time(&ts);
  info = localtime(&ts);
  int len = (int)strftime(wptr, remainBufferSz, "%Y-%m-%d %H:%M:%S", info);
  wptr += len;
  remainBufferSz -= len;

  Uint32 ms = SDL_GetTicks() % 1000;

  len = snprintf(wptr, remainBufferSz, ".%03u %c/%s:%d/%s | ", ms, chLevel, fn, ln, func);
  wptr += len;
  remainBufferSz -= len;

  len = vsnprintf(wptr, remainBufferSz, fmt, vargs);
  wptr += len;
  remainBufferSz -= len;
  
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
