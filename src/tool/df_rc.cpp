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
  auto gmap = restoreFromDirForDFree();
  makeDycky(gmap);
  printStatistic(gmap);

  auto gw = convertForFLARE(gmap, ID);

  vector<string> srcs, sinks;
  read_query_for_dfree(srcs, sinks);

  vector<uint64_t> translated_query[2];
  for (auto src : srcs) {
    translated_query[0].push_back(gw->getNodeIdx(src));
  }
  for (auto sink : sinks) {
    translated_query[1].push_back(gw->getNodeIdx(sink));
  }

  bool ret;
  uint total_pairs = 0, id_reach = 0, both_reach = 0, unique_src = 0,
       total_src = 0;
  // search for id leak
  auto tic = std::chrono::high_resolution_clock::now();
  auto leak_q = new GrailWrapper{convertForFLARE(gmap, ID)};
  leak_q->index(ID_DEC_ONCE);
  for (auto src : translated_query[0]) {
    auto src_reach = false;
    ++total_src;
    for (auto sink : translated_query[1]) {
      total_pairs++;
      auto ret = leak_q->reach(src, sink);
      src_reach |= ret;
      id_reach += ret;
    }
    if (src_reach) {
      ++unique_src;
    }
  }
  auto toc = std::chrono::high_resolution_clock::now();
  outs() << fmt::format("total: {}\tcs id reach: {}\tboth "
                        "reach: {}\n",
                        total_pairs, id_reach, both_reach);
  outs() << fmt::format("total_src: {}\tunique_src: {}\n", total_src,
                        unique_src);
  outs() << fmt::format(
      "time: {} ms\n",
      chrono::duration_cast<chrono::milliseconds>(toc - tic).count());

  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  cout << "Memory usage: " << ru.ru_maxrss / 1024.0 << " MB" << endl;
  return 0;
}