// generate query for inc/dec
#include "Graph.h"
#include "GraphWrapper.h"
#include "RGraph.h"
#include "fmt/format.h"
#include "llvm/Support/raw_ostream.h"
#include <chrono>
#include <cstdlib>
#include <cstring>

bool enable_test = true;

void constructFrom(rc::GraphCollection &gmap, rc::Node *node) {
  unordered_set<rc::Node *> visited;
  stack<rc::Node *> worklist;
  worklist.push(node);
  visited.insert(node);
  unordered_map<rc::Graph *, rc::Graph *> gm;
  auto getNewNode = [&](rc::Node *n) {
    rc::Graph *ng;
    auto it = gm.find(n->Parent);
    if (it == gm.end()) {
      ng = new rc::Graph;
      ng->fn_name = n->Parent->fn_name;
      gmap[n->Parent->fn_name].push_back(ng);
      gm[n->Parent] = ng;
    } else {
      ng = it->second;
    }

    auto nn = ng->getNodeIfExists(n->getId());
    if (nn) {
      return nn;
    } else {
      return n->json->into(ng);
    }
  };
  while (!worklist.empty()) {
    auto n = worklist.top();
    worklist.pop();

    auto nn = getNewNode(n);

    for (auto e : n->Out) {
      if (e->Kind == rc::Edge::RET && n->Parent == node->Parent) {
        continue;
      }
      auto next = e->Dst;
      auto nnext = getNewNode(next);
      auto ne = new rc::Edge;
      ne->Src = nn;
      ne->Dst = nnext;
      ne->Kind = e->Kind;
      ne->label = e->label;
      ne->connect();

      if (visited.insert(next).second) {
        worklist.push(next);
      }
    }
  }
}

void verify(rc::GraphWrapper *gw, rc::GraphCollection &gmap) {
  uint ori_edge_cnt = 0, new_edge_cnt = 0;
  for (auto &p : gmap) {
    for (auto g : p.second) {
      for (auto n : g->NodeSet) {
        auto s = gw->getNodeIdxFromPtr(n);
        auto &outlist = gw->flare_graph->graph[s].outList;
        for (auto e : n->Out) {
          ori_edge_cnt++;
          auto d = gw->getNodeIdxFromPtr(e->Dst);
          bool hit = false;
          for (auto dit : outlist) {
            if (dit == d) {
              hit = true;
              break;
            }
          }
          assert(hit);
        }
      }
    }
  }
  for (auto i = 0; i < gw->flare_graph->num_vertices(); ++i) {
    new_edge_cnt += gw->flare_graph->graph[i].outList.size();
  }
  assert(new_edge_cnt == ori_edge_cnt);
}

void dfs(rc::GraphWrapper *gw, vector<uint> &level, vector<uint8_t> &visit,
         uint node, uint level_id, uint visit_id) {
  errs() << fmt::format(
      "At lvl {}: {} [{}] {}\n", level_id, gw->getNode(node)->getId(),
      gw->getNode(node)->json->site, gw->getNode(node)->Parent->fn_name);
  for (auto n : gw->flare_graph->graph[node].outList) {
    if (!visit[n]) {
      visit[n] = visit_id;
      level[n] = level_id;
      dfs(gw, level, visit, n, level_id + 1, 1);
    } else {
      errs() << fmt::format(
          "Found back edge!!!!\n"
          "{} {} {} {} level: {} -> {} {} {} {} level: {}\n",
          gw->getNode(node)->getId(), gw->getNode(node)->json->site,
          gw->getNode(node)->getKindName(), gw->getNode(node)->Parent->fn_name,
          level[node], gw->getNode(n)->getId(), gw->getNode(n)->json->site,
          gw->getNode(n)->getKindName(), gw->getNode(n)->Parent->fn_name,
          level[n]);
      assert(false);
    }
  }
}

void backEdgeDetect(rc::GraphWrapper *gw) {
  vector<uint> level(gw->flare_graph->num_vertices());
  vector<uint8_t> visit(gw->flare_graph->num_vertices());
  for (auto i = 0; i < gw->flare_graph->num_vertices(); ++i) {
    if (gw->flare_graph->graph[i].inList.empty()) {
      visit[i] = 1;
      errs() << fmt::format("Start from {} [{}] {}\n", gw->getNode(i)->getId(),
                            gw->getNode(i)->json->site,
                            gw->getNode(i)->Parent->fn_name);
      dfs(gw, level, visit, i, 0, 1);
    }
  }
}

bool verify_leak(vector<int> &trace, _Graph *g) {
  int cnt = 0;
  for (size_t i = 1; i < trace.size(); ++i) {
    auto prev = trace[i - 1];
    auto curr = trace[i];
    auto &edges = g->graph[prev].outList;
    auto is_edge = std::find(edges.begin(), edges.end(), curr) != edges.end();
    assert(is_edge);
    auto is_pos_edge = g->is_pos_edge(prev, curr);
    auto is_neg_edge = g->is_neg_edge(prev, curr);
    if (is_pos_edge) {
      cnt++;
    } else if (is_neg_edge) {
      cnt--;
    }
  }
  // errs() << cnt << '\n';
  return cnt > 0;
}

int main() {
  using namespace rc;
  using namespace std;
  struct {
    unordered_set<std::string> src, sink;

    void dump(ofstream &out) {
      for (auto s : src) {
        out << s << " ";
      }
      out << '\n';
      for (auto s : sink) {
        out << s << " ";
      }
    }
  } df, leak;

  string input = get_subject();
  auto gmap = restoreFromDirForLeak();
  makeDycky(gmap);
  printStatistic(gmap);
  for (auto &p : gmap) {
    for (auto g : p.second) {
      for (auto n : g->NodeSet) {
        if (n->Kind == Node::INC) {
          leak.src.insert(n->getId());
        } else if (n->Kind == Node::OOS) {
          leak.sink.insert(n->getId());
        }
      }
    }
  }
  ofstream mout("ml_" + input + "_query_pairs.txt");
  assert(mout.good());
  leak.dump(mout);
  mout.close();
  outs() << "memory leak: " << leak.src.size() << ' ' << leak.sink.size()
         << " > "
         << "ml_" + input + "_query_pairs.txt" << '\n';

  gmap = restoreFromDirForDFree();
  makeDycky(gmap);
  printStatistic(gmap);
  for (auto &p : gmap) {
    for (auto g : p.second) {
      for (auto n : g->NodeSet) {
        if (n->In.empty() && n->Kind != Node::DEC && n->Kind != Node::ARG) {
          df.src.insert(n->getId());
        } else if (n->In.size() == 1 && (*n->In.begin())->Kind == Edge::DEC) {
          df.sink.insert(n->getId());
        }
      }
    }
  }
  ofstream dout("df_" + input + "_query_pairs.txt");
  df.dump(dout);
  dout.close();
  outs() << "double free: " << df.src.size() << ' ' << df.sink.size()
         << " > df_" + input + "_query_pairs.txt\n";
  return 0;
}