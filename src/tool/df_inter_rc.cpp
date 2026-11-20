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
       total_src = 0, i = 0;
  ofstream out("rc_answers.txt");
  // search for id leak
  auto tic = std::chrono::high_resolution_clock::now();
  auto ret_q = new GrailWrapper{convertForFLARE(gmap, CS)};
  ret_q->index(CS_FULL);
  auto leak_q = new GrailWrapper{convertForFLARE(gmap, ID)};
  leak_q->index(ID_DEC_ONCE);
  for (auto sink : translated_query[1]) {
    auto src_ok = true;
    ++total_src;
    uint _src = 0;
    for (auto src : translated_query[0]) {
      total_pairs++;
      auto ret = leak_q->reach(src, sink);
      if (ret) {
        id_reach++;
        auto ret = ret_q->reach(src, sink);
        if (ret) {
          src_ok = false;
          _src = src;
          both_reach++;
          break;
        }
      }
    }
    if (!src_ok) {
      ++unique_src;
      out << ++i << ' ' << gw->getNode(_src)->getId() << ' '
          << gw->getNode(_src)->json->site << ' '
          << gw->getNode(_src)->Parent->fn_name << "\n      "
          << gw->getNode(sink)->getId() << ' '
          << gw->getNode(sink)->json->site << ' '
          << gw->getNode(sink)->Parent->fn_name << '\n';
    }
  }
  auto toc = std::chrono::high_resolution_clock::now();
  outs() << fmt::format("total: {}\tcs id reach: {}\tboth "
                        "reach: {}\n",
                        total_pairs, id_reach, both_reach);
  outs() << fmt::format("total_src: {}\tunique_src: {}\n", total_src,
                        unique_src);
  outs() << fmt::format(
      "time: {}ms\n",
      chrono::duration_cast<chrono::milliseconds>(toc - tic).count());

  return 0;
}