#include "FUtil.h"
#include "RGraph.h"
#include "WPDS.h"
#include "ref_ptr.h"
#include <bits/stdc++.h>
#include <cassert>
using namespace std;

// ---------- Boolean Reach semiring (WPDS++ manual §2.8) ----------
class Reach {
  bool isreached;

public:
  ref_ptr<Reach>::count_t count;
  Reach(bool b) : isreached(b), count(0) {}
  Reach *one() const { return new Reach(true); }
  Reach *zero() const { return new Reach(false); }
  Reach *quasiOne() const { return one(); }
  Reach *extend(Reach *rhs) const {
    return (isreached && rhs->isreached) ? one() : zero();
  } // ⊗
  Reach *combine(Reach *rhs) const {
    return (isreached || rhs->isreached) ? one() : zero();
  } // ⊕
  bool equal(Reach *rhs) const { return isreached == rhs->isreached; }
  std::ostream &print(std::ostream &o) const {
    return o << (isreached ? "ONE" : "ZERO");
  }
  bool isOne() const { return isreached; }
};

wpds::wpds_key_t A, Z, ACC, ROOT;
vector<wpds_key_t> alphabet;

// Build the WPDS from a labeled graph. One stack symbol A for '('; Z is bottom.
static void build_wpds(rc::GraphCollection &gmap, wpds::WPDS<Reach> &PDS) {
  A = str2key("A"), Z = str2key("Z");
  alphabet = {A, Z};

  Reach *one = new Reach(true);

  for (auto &pair : gmap) {
    for (auto g : pair.second) {
      for (auto n : g->NodeSet) {
        for (auto e : n->Out) {
          auto p = str2key((e->Src->getId())), q = str2key((e->Dst->getId()));
          switch (e->Kind) {
          case rc::Edge::INC:
            for (auto X : alphabet) {
              PDS.add_rule(p, X, q, A, X, one); // push  (1-symbol RHS)
            }
            break;
          case rc::Edge::DEC:
            PDS.add_rule(p, A, q, one); // pop  (0-symbol RHS)
            break;
          default:
            for (auto X : alphabet) {
              PDS.add_rule(p, X, q, X, one); // neutral
            }
          }
        }
      }
    }
  }
}

// models context-sensitive in a reverse manner
// push )_i for return and pop )_i for call
// search starts from sink
static void build_wpds_for_cs(rc::GraphCollection &gmap,
                              wpds::WPDS<Reach> &PDS) {
  A = str2key("A"), Z = str2key("Z");
  alphabet = {A, Z};

  Reach *one = new Reach(true);

  for (auto &pair : gmap) {
    for (auto g : pair.second) {
      for (auto n : g->NodeSet) {
        for (auto e : n->Out) {
          auto p = str2key((e->Src->getId())), q = str2key((e->Dst->getId()));
          switch (e->Kind) {
          case rc::Edge::CALL:
            PDS.add_rule(q, A, p, one); // pop  (0-symbol RHS)
            break;
          case rc::Edge::RET:
            for (auto X : alphabet) {
              PDS.add_rule(q, X, p, A, X, one); // push  (1-symbol RHS)
            }
            break;
          default:
            for (auto X : alphabet) {
              PDS.add_rule(q, X, p, X, one); // neutral
            }
          }
        }
      }
    }
  }
}

// Build a CA that accepts **A⁺Z** at control t  (Strict-prefix target).
static wpds::CA<Reach> make_prefix_target(wpds::wpds_key_t InitCtrlLoc,
                                          wpds::Semiring<Reach> &S) {
  wpds::CA<Reach> ca(S);
  ACC = str2key("ACC_PFX");
  Reach *one = new Reach(true);
  ca.add_initial_state(InitCtrlLoc);
  ca.add_final_state(ACC);
  ca.add(InitCtrlLoc, Z, ACC, one);
  return ca;
}

bool reachable(wpds_key_t sink, CA<Reach> &ans) {
  auto &rules = ans.get_rules();
  auto it = rules.find(sink);
  if (it != rules.end()) {
    for (auto &tran : it->second) {
      assert(tran->from_state() == sink);
      if (tran->transition() == A) {
        return true;
      }
    }
  }
  return false;
}

int main(int argc, char **argv) {
  using namespace rc;
  using namespace std;
  auto gmap = restoreFromDirForLeak(get_input_dir());
  makeDycky(gmap);
  printStatistic(gmap);

  vector<string> srcs, sinks;
  read_query(srcs, sinks);

  vector<wpds_key_t> translated_query[2];
  for (auto src : srcs) {
    translated_query[0].push_back(str2key(src));
  }
  for (auto sink : sinks) {
    translated_query[1].push_back(str2key(sink));
  }

  // Semiring & WPDS (WPDS++ §2.8, §3)
  Reach *reachOne = new Reach(true);
  wpds::Semiring<Reach> S(reachOne);
  wpds::WPDS<Reach> ID_PDS(S);
  wpds::WPDS<Reach> CS_PDS(S);

  unordered_map<string, wpds::wpds_key_t> K;

  auto start = std::chrono::high_resolution_clock::now();
  build_wpds(gmap, ID_PDS);
  build_wpds_for_cs(gmap, CS_PDS);

  uint total_pairs = 0, yes_pairs = 0, i = 0;
  unordered_map<wpds_key_t, vector<wpds_key_t>> reach_pairs;
  for (auto &src : translated_query[0]) {
    auto target1 = make_prefix_target(src, S);
    auto ans1 = wpds::poststar<Reach>(ID_PDS, target1, S); // compute Post*
    for (auto sink : translated_query[1]) {
      ++total_pairs;
      if (reachable(sink, ans1)) {
        reach_pairs[sink].push_back(src);
        ++yes_pairs;
      }
    }
    outs() << "\r" << ++i << " / " << translated_query[0].size();
  }

  i = 0;
  for (auto &item : reach_pairs) {
    auto sink = item.first;
    auto target2 = make_prefix_target(sink, S);
    auto ans2 = wpds::poststar<Reach>(CS_PDS, target2, S); // compute Post*
    // ans2.path_summary();
    for (auto src : item.second) {
      if (ans2.state_weight(src)->isOne()) {
        ++yes_pairs;
      }
    }
    outs() << "\r" << ++i << " / " << reach_pairs.size();
  }

  auto end = std::chrono::high_resolution_clock::now();
  cout << "Queries in: "
       << std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count()
       << " ms" << endl;
  cout << "total_pairs: " << total_pairs << ", yes_pairs: " << yes_pairs
       << endl;
  return 0;
}
