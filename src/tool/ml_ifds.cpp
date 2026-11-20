#include "FUtil.h"
#include "GraphWrapper.h"
#include "RGraph.h"
#include "fmt/format.h"
#include "ide.h"
#include "llvm/Support/raw_ostream.h"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>

using namespace std;

int main() {
  using namespace rc;
  using namespace std;
  auto gmap = restoreFromDirForLeak(get_input_dir());
  makeDycky(gmap);
  printStatistic(gmap);

  vector<string> srcs, sinks;
  read_query(srcs, sinks);

  auto gw = convertForFLARE(gmap, rc::DT_NONE);
  ofstream out("ide_answers.txt");

  vector<Node *> translated_query[2];
  for (auto &src_hash : srcs) {
    auto src = gw->getNodeFromHash(src_hash);
    translated_query[0].push_back(src);
  }
  for (auto &src_hash : sinks) {
    auto src = gw->getNodeFromHash(src_hash);
    translated_query[1].push_back(src);
  }

  uint id_reach = 0, total_pairs = 0, src_cnt = 0, leak_src = 0;
  // search for id leak
  auto tic = std::chrono::high_resolution_clock::now();
  IdeSolver _solver(&gmap);
  auto solver = &_solver;
  uint i = 0;
  for (auto src : translated_query[0]) {
    bool src_ok = true;
    solver->index(src);
    for (auto sink : translated_query[1]) {
      total_pairs++;
      auto ret = solver->reach(src, sink);
      if (ret) {
        src_ok = false;
        id_reach++;
      }
    }
    if (!src_ok) {
      ++leak_src;
      out << src->getId() << ' ' << (src_ok ? 0 : 1) << endl;
    }
    outs() << i++ << " / " << translated_query[0].size() << '\r';
  }
  auto toc = std::chrono::high_resolution_clock::now();
  out.close();
  outs() << fmt::format(
      "total pair: {}\ttotal_src: {}\tboth reach: {}\tleak_src: {}\n",
      total_pairs, srcs.size() * sinks.size(), id_reach, leak_src);
  outs() << fmt::format(
      "time: {} ms\n",
      chrono::duration_cast<chrono::milliseconds>(toc - tic).count());
  return 0;
}