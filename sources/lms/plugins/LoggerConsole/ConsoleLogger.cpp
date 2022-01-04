#include <lms/Logger.h>
#include <mutex>

namespace lms {

class LogWriterConsole : public LogWriter {
public:
  void write(const char *log) override {
//    std::lock_guard<std::mutex> guard(mtx);
    printf("%s", log);
  }

private:
  // printf是非线程安全的，所以需要手工使用一个互斥量来解决串行输出的问题。否则，会导致内容交错，如假设有aaaa,bbbb两行日志
  // 正确输出应该是
  //   aaaa
  //   bbbb
  // 可能的输出
  //   aabbbb
  //   aa
  std::mutex mtx;
};

LogWriter *createLogWriter() {
  return new LogWriterConsole;
}

}
