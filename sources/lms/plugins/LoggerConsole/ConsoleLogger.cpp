#include <lms/Logger.h>
#include <mutex>

namespace lms {

class LogWriterConsole : public LogWriter {
public:
  void write(const char *log) override {
    printf("%s", log);
  }
};

LogWriter *createLogWriter() {
  return new LogWriterConsole;
}

}
