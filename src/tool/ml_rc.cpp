#include "FUtil.h"
#include "Graph.h"
#include "GraphWrapper.h"
#include "RGraph.h"
#include "fmt/format.h"
#include "llvm/Support/raw_ostream.h"
#include <chrono>
#include <cstring>

int main() {
  using namespace rc;
  using namespace std;
  auto gmap = restoreFromDirForLeak();
  makeDycky(gmap);
  printStatistic(gmap);

  auto gw = convertForFLARE(gmap, ID);

  vector<string> srcs, sinks;
  read_query_for_leak(srcs, sinks);

  vector<uint64_t> translated_query[2];
  for (auto src : srcs) {
    translated_query[0].push_back(gw->getNodeIdx(src));
  }
  for (auto sink : sinks) {
    translated_query[1].push_back(gw->getNodeIdx(sink));
  }

  uint total_pairs = 0, id_reach = 0;
  // search for id leak
  auto leak_q = new GrailWrapper{convertForFLARE(gmap, ID)};
  std::chrono::high_resolution_clock::time_point tic, toc;
  total_pairs = 0, id_reach = 0;

  tic = std::chrono::high_resolution_clock::now();
  leak_q->index(ID_INC_ONCE);
  for (auto &src : translated_query[0]) {
    auto src_reach = false;
    for (auto sink : translated_query[1]) {
      total_pairs++;
      auto ret = leak_q->reach(src, sink);
      if (ret) {
        ++id_reach;
      }
    }
  }
  toc = std::chrono::high_resolution_clock::now();
  outs() << fmt::format("total: {}\tcs id reach: {}\n", total_pairs, id_reach);
  outs() << fmt::format(
      "time: {} ms\n",
      chrono::duration_cast<chrono::milliseconds>(toc - tic).count());
  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  cout << "Memory usage: " << ru.ru_maxrss / 1024.0 << " MB" << endl;
  return 0;
}