#ifndef FUTIL_H
#define FUTIL_H

#include "RUtil.h"
#include "fmt/format.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <deque>
#include <fstream>
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Function.h>
#include <random>
#include <sstream>
#include <sys/resource.h>
#include <vector>

namespace fmt {
template <> struct formatter<llvm::StringRef> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
  auto format(llvm::StringRef const &c, format_context &ctx) const {
    return format_to(ctx.out(), "{}", c.str());
  }
};
} // namespace fmt

inline bool topsortCFG(std::vector<llvm::BasicBlock const *> &bb_sorted,
                       llvm::Function const *F, bool report_cycle) {
  using namespace llvm;
  using namespace std;
  unordered_map<llvm::BasicBlock const *, int> in_deg;
  deque<llvm::BasicBlock const *> worklist;

  // Count the in-degree of all the basic blocks
  for (auto &bb_ref : *F) {
    auto *bb = &bb_ref;

    int num = 0;
    for (auto PI = pred_begin(bb), PE = pred_end(bb); PI != PE; ++num, ++PI) {
    }
    in_deg[bb] = num;

    //        for (succ_iterator I = succ_begin(bb), E = succ_end(bb); I != E;
    //        ++I) {
    //            BasicBlock* succ_bb = *I;
    //            in_deg[succ_bb]++;
    //        }
  }

  for (auto &bb_ref : *F) {
    auto *bb = &bb_ref;
    if (in_deg[bb] == 0) {
      worklist.push_back(bb);
    }
  }

  while (!worklist.empty()) {
    auto *bb = worklist.front();
    worklist.pop_front();

    // Relax the successor nodes
    for (auto I = succ_begin(bb), E = succ_end(bb); I != E; ++I) {
      auto *succ_bb = *I;
      if (--in_deg[succ_bb] == 0) {
        worklist.push_back(succ_bb);
      }
    }

    bb_sorted.push_back(bb);
  }

  int nc = 0;

  if (report_cycle) {
    for (auto &bb_ref : *F) {
      auto *bb = &bb_ref;
      if (in_deg[bb] != 0) {
        nc++;
        //                dbgs() << "Cycle member: " << bb->getName() << ",
        //                degree: " <<in_deg[bb] << "\n";
      }
    }
  }

  return nc == 0;
}

inline void read_query(std::vector<std::string> &srcs,
                       std::vector<std::string> &sinks,
                       std::string const &mode) {
  std::ifstream in(mode + '_' + get_subject() + "_query_pairs.txt");
  llvm::outs() << "Reading query pairs from "
               << mode + '_' + get_subject() + "_query_pairs.txt"
               << "\n";
  std::string line, v;
  bool has_src_line = static_cast<bool>(getline(in, line));
  assert(has_src_line);
  std::stringstream ss(line);
  while (ss >> v) {
    srcs.push_back(v);
  }
  bool has_sink_line = static_cast<bool>(getline(in, line));
  assert(has_sink_line);
  std::stringstream ss1(line);
  while (ss1 >> v) {
    sinks.push_back(v);
  }
  llvm::outs() << "#Src: " << srcs.size() << ", #Sink: " << sinks.size()
               << "\n";
}

inline void read_query(std::vector<std::string> &srcs,
                       std::vector<std::string> &sinks) {
  read_query(srcs, sinks, "ml");
}

inline void read_query_for_leak(std::vector<std::string> &srcs,
                                std::vector<std::string> &sinks) {
  read_query(srcs, sinks, "ml");
}

inline void read_query_for_dfree(std::vector<std::string> &srcs,
                                 std::vector<std::string> &sinks) {
  read_query(srcs, sinks, "df");
}

inline bool time_index_only() {
  auto val = getenv("t");
  if (!val)
    return false;
  return std::string(val) == "idx";
}

inline bool time_query_only() {
  auto val = getenv("t");
  if (!val)
    return false;
  return std::string(val) == "qry";
}

inline void drop_rate(std::vector<uint64_t> &v, uint rate) {
  std::random_device r;
  std::default_random_engine e1(r());
  std::uniform_int_distribution<int> uniform_dist(1, 10);

  size_t len = v.size();
  for (size_t i = 0; i < len;) {
    if (uniform_dist(e1) <= rate) {
      std::swap(v[i], v[len - 1]);
      len--;
    } else {
      i++;
    }
  }
  llvm::outs() << "Dropped: " << int((v.size() - len) * 100 / v.size())
               << " %\n";
  v.resize(len);
}

#endif
