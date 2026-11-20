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

  auto gw = convertForFLARE(gmap, rc::DT_NONE);
  auto ret_q = new GrailWrapper{convertForFLARE(gmap, CS)};
  ret_q->index(CS_FULL);
  auto leak_q = new GrailWrapper{convertForFLARE(gmap, ID)};
  leak_q->index(ID_DEC_ONCE);

  vector<string> srcs, sinks;
  read_query_for_dfree(srcs, sinks);

  vector<uint64_t> translated_query[2];
  for (auto src : srcs) {
    translated_query[0].push_back(gw->getNodeIdx(src));
  }
  for (auto sink : sinks) {
    translated_query[1].push_back(gw->getNodeIdx(sink));
  }

  uint total = 0;
  uint id_reach = 0, cs_reach = 0, both = 0, none = 0;
  // search for id leak
  auto start = std::chrono::high_resolution_clock::now();
  for (auto sink : translated_query[1]) {
    auto src_cs = false;
    auto src_leak = false;
    auto src_both = false;
    total++;
    for (auto src : translated_query[0]) {
      auto ret2 = ret_q->reach(src, sink);
      auto ret1 = leak_q->reach(src, sink);
      if (ret1 && ret2) {
        src_both = true;
      }
      if (ret1) {
        src_leak = true;
      }
      if (ret2) {
        src_cs = true;
      }
    }
    if (src_both) {
      both++;
    }
    if (src_leak) {
      id_reach++;
    }
    if (src_cs) {
      cs_reach++;
    }
  }
  auto end = std::chrono::high_resolution_clock::now();
  cout << "Query Duration: "
       << std::chrono::duration_cast<std::chrono::seconds>(end - start).count()
       << " s" << endl;
  outs() << fmt::format("id: {}\tcs: {}\tboth: {}\n", id_reach, cs_reach, both);
  outs() << fmt::format("total: {}\t[cs: {:.4f}]\t[id: "
                        "{:.4f}]\t[both / total: {:.4f}]\n",
                        total, (cs_reach)*1.0f / total, (id_reach)*1.0f / total,
                        both * 1.0f / total);
  return 0;
}