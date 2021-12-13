#include "StreamBuilder.h"
#include <set>
#include <vector>

namespace lms {

class PathSolver {
public:
  void addNode(void *node) {
    nodes.insert(node);
  }
  
  void setEndpoints(void *start, std::set<void *> stops) {
    this->start = start;
    this->stops = stops;
  }
  
  void link(void *from, void *to) {
    
  }
  
  std::list<void *> solve() {
    std::list<void *> path;

    return path;
  }
  
private:
  void *start;
  std::set<void *> stops;
  std::set<void *> nodes;
  std::map<void *, void *> linkages;
};

class Filter;

class FilterFactory : virtual public Object {
public:
  virtual Filter *createFilter() = 0;
  virtual bool supports(const StreamContext& sc) = 0;
  virtual std::string inputSpec() const = 0;
  virtual std::string outputSpec() const = 0;
};

struct FilterBuilder {
  std::set<FilterFactory *> candidateFactories() {
    return {};
  }
};

static FilterBuilder _filterBuilders[] = {
};

class DirectFactory : public FilterFactory {
public:
  DirectFactory(Filter *filter) {
    this->filter = filter;
  }
  
  Filter *createFilter() override {
    return filter;
  }
  
  bool supports(const StreamContext& sc) override {
    return true;
  }
  
  std::string inputSpec() const override {
    return "";
  }
  
  std::string outputSpec() const override {
    return "";
  }
  
private:
  Filter *filter;
};

Stream *createStream(MediaSource *source, std::vector<Render *> renders) {
  const StreamContext& sc = source->streamContext();
  
  PathSolver ps;
  std::set<FilterFactory *> allFactories;
  
  auto srcFilterFactory = new DirectFactory((Filter *)source);
  ps.addNode(srcFilterFactory);
  allFactories.insert(srcFilterFactory);
  
  for (auto r : renders) {
    auto renderFactory = new DirectFactory((Filter *)r);
    if (renderFactory->supports(sc)) {
      ps.addNode(renderFactory);
      allFactories.insert(renderFactory);
    }
  }
    
  for (auto b : _filterBuilders) {
    std::set<FilterFactory *> factories = b.candidateFactories();

    for (auto f : factories) {
      if (f->supports(sc)) {
        ps.addNode(f);
        allFactories.insert(f);
      }
    }
  }
  
  for (auto from: allFactories) {
    for (auto to: allFactories) {
      if (from == to) {
        // 避免自连接
        continue;
      }
      
      if (from->outputSpec() == to->inputSpec()) {
        ps.link(from, to);
      }
    }
  }
  
  auto path = ps.solve();
  
  if (path.empty()) {
    return nullptr;
  }
  
  Stream *stream;  // TODO: new Stream
  
  for (auto f : path) {
    // TODO: stream->append(f->createFilter());
  }
  
  // TODO: Release resources above
  
  return stream;
}

}
