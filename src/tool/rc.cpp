#include "Graph.h"
#include "GraphWrapper.h"
#include "RGraph.h"
#include "fmt/format.h"
#include "llvm/Support/raw_ostream.h"
#include <chrono>
#include <cstring>

struct SPDS {
  vector<rc::GrailWrapper *> grails;
  vector<int> trace;
  vector<vector<int>> traces;
  int8_t *visited = nullptr;
  _Graph *g = nullptr;
  int* sccmap = nullptr;
  bool terminated = false;
  const uint max_trace = 20;

  enum ReachResult { NIL = 0, UNREACH, VISITED };

  void init(size_t size) {
    traces.clear();
    trace.clear();
    if (!visited)
      visited = new int8_t[size];
    else
      memset(visited, NIL, sizeof(int8_t) * size);
  }

  bool validate_next(uint s, uint t) {
    switch (visited[s]) {
    case NIL:
      break;
    case UNREACH:
      return false;
    case VISITED:
      return false;
    }
    for (auto q : grails) {
      if (!q->fast_check(s, t)) {
        visited[s] = UNREACH;
        return false;
      }
    }
    return true;
  }

  bool dfs(uint s, uint t) {
    bool ret = false;
    trace.push_back(s);
    visited[s] = VISITED;
    if (s == t) {
      ret = true;
      traces.push_back(trace);
      if (traces.size() >= max_trace) {
        terminated = true;
        return true;
      }
      goto out;
    }
    for (auto n : g->graph[s].outList) {
      if (!validate_next(n, t)) {
        continue;
      }
      if (dfs(n, t)) {
        ret = true;
        if (terminated) {
          return true;
        }
      }
    }
  out:
    trace.pop_back();
    if (visited[s] == VISITED) {
      visited[s] = NIL;
    }
    return ret;
  }

  bool reach(uint s, uint t) {
    return dfs(s, t);
  }
};

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
        auto s = gw->getNodeIdx(n);
        auto &outlist = gw->flare_graph->graph[s].outList;
        for (auto e : n->Out) {
          ori_edge_cnt++;
          auto d = gw->getNodeIdx(e->Dst);
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
  auto gmap = restoreFromDirForLeak("input");
  // auto gmap = restoreFromDir("input");
  // transform
  makeDycky(gmap);
  printStatistic(gmap);
  // StringSet<> funcs;
  // funcs.insert("release_mid");
  // funcs.insert("cifs_is_path_accessible");
  // funcs.insert("smb2_readv_callback");
  // dumpGraph(gmap, "imported-select.dot", funcs);
  dumpGraph(gmap, "imported.dot");

  auto gw = convertForFLARE(gmap, ID);
  auto *plain_q = new GrailWrapper{convertForFLARE(gmap, rc::DT_NONE)};
  plain_q->index(GT_NONE);
  auto ret_q = new GrailWrapper{convertForFLARE(gmap, CS)};
  ret_q->index(CS_RET);
  auto leak_q = new GrailWrapper{convertForFLARE(gmap, ID)};
  leak_q->index(ID_INC_ONCE);

  // locate oos and root of each function as source sink candidates
  unordered_set<Node *> Root, OOS;
  for (auto &p : gmap) {
    for (auto g : p.second) {
      for (auto n : g->NodeSet) {
        if (n->In.empty() && n->Kind != rc::Node::ARG) {
          Root.insert(n);
        } else if (n->Kind == Node::OOS) {
          OOS.insert(n);
        } else if (n->Kind == Node::RET) {
          OOS.insert(n);
        }
      }
    }
  }
  outs() << fmt::format("root: {}\toos: {}\n", Root.size(), OOS.size());
  uint id_reach = 0, cs_ret_reach = 0, both_reach = 0, i = 0, total_pairs = 0;
  // search for id leak
  unordered_map<Node *, vector<Node *>> SrcSinkPairs;
  auto start = std::chrono::high_resolution_clock::now();
  for (auto r : Root) {
    for (auto o : OOS) {
      auto ret0 =
          plain_q->reach(gw->getNodeIdxStrict(r), gw->getNodeIdxStrict(o));
      if (!ret0)
        continue;
      total_pairs++;
      auto ret1 =
          leak_q->reach(gw->getNodeIdxStrict(r), gw->getNodeIdxStrict(o));
      if (ret1) {
        id_reach++;
      }
      auto ret2 =
          ret_q->reach(gw->getNodeIdxStrict(r), gw->getNodeIdxStrict(o));
      if (ret2) {
        cs_ret_reach++;
      }
      if (ret1 && ret2) {
        both_reach++;
        SrcSinkPairs[r].push_back(o);
      }
    }
  }
  auto end = std::chrono::high_resolution_clock::now();
  cout << "Query Duration: "
       << std::chrono::duration_cast<std::chrono::seconds>(end - start).count()
       << " s" << endl;
  outs() << fmt::format("total: {}\tid reach: {}\tcs ret reach: {}\tboth "
                        "reach: {}\tUnique Src: {}\n",
                        total_pairs, id_reach, cs_ret_reach, both_reach,
                        SrcSinkPairs.size());
  return 0;
  //////////////////////
  // refine query result
  //////////////////////
  start = std::chrono::high_resolution_clock::now();
  SPDS pds{{ret_q}};
  pds.g = gw->flare_graph;
  pds.init(pds.g->num_vertices());
  int ti = 0, step = 0;
  for (auto &p : SrcSinkPairs) {
    auto src = p.first;
    bool reported = false;
    for (auto sink : p.second) {
    pds.init(pds.g->num_vertices());
      auto ret =
          pds.reach(gw->getNodeIdxStrict(src), gw->getNodeIdxStrict(sink));
      if (!ret)
        continue;
      size_t j = 0;
      for (size_t i = 0; i < pds.traces.size(); ++i) {
        auto &trace = pds.traces[i];
        if (verify_leak(pds.traces[i], pds.g)) {
          if (i != j)
            pds.traces[j] = trace;
          j++;
        }
      }
      errs() << "Filtered non leak path"
             << fmt::format(" {} -> {}\n", pds.traces.size(), j);
      pds.traces.resize(j);
      if (j == 0)
        continue;
      errs() << "######################################\n";
      errs() << "Trace Collection " << ti++ << "\n";
      for (auto &trace : pds.traces) {
        step = 0;
        for (auto node : trace) {
          auto n = gw->getNode(node);
          errs() << fmt::format("{} {} @ {}\n", step++, n->json->site,
                                n->Parent->fn_name);
        }
        errs() << "------------------------------------\n";
        reported = true;
        break;
      }
      if (reported) {
        break;
      }
    }
  }

  end = std::chrono::high_resolution_clock::now();
  cout << "SPDS Duration: "
       << std::chrono::duration_cast<std::chrono::seconds>(end - start).count()
       << " s" << endl;
  return 0;
}