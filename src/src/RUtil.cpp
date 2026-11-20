#include "RUtil.h"
#include "Graph.h"
#include <cstdint>
#include <utility>
#include "fmt/format.h"
#include "llvm/Support/FileSystem.h"

struct p_cmp {
  bool operator()(std::pair<uint64_t, uint64_t> const &p1,
                  std::pair<uint64_t, uint64_t> const &p2) const {
    return p1.first == p2.first && p1.second == p2.second;
  }
};

struct p_hash {
  std::size_t operator()(const std::pair<uint64_t, uint64_t> &p) const {
    std::hash<uint64_t> h;
    return h(p.first) ^ h(p.second);
  }
};

void rc::dumpFLARE(rc::GraphWrapper *gw, StringRef filename) {
  std::error_code ec;
  raw_fd_ostream fo(filename, ec, llvm::sys::fs::F_Text);
  fo << "digraph G{\n";
  auto &edges = gw->flare_graph->graph;
  for (size_t sid = 0; sid < edges.size(); ++sid) {
    auto &edge = edges[sid];
    auto n = gw->getNode(sid);
    for (auto tid : edge.outList) {
      fo << fmt::format("\ta{} -> a{}", n->getId(), gw->getNode(tid)->getId());
      if (gw->flare_graph->is_smry_edge(sid, tid)) {
        fo << fmt::format("[label=\"summary\"]");
      } else if (auto label = gw->flare_graph->is_pos_edge(sid, tid)) {
        fo << fmt::format("[label=\"{}\"]", label);
      } else if (auto label = gw->flare_graph->is_neg_edge(sid, tid)) {
        fo << fmt::format("[label=\"{}\"]", label);
      }
      fo << ";\n";
    }
  }
  fo << "}\n";
}