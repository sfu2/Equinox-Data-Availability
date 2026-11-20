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

wpds::wpds_key_t A, Z, ACC;
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
            PDS.add_rule(q, A, p, one); // pop  (0-symbol RHS)
            break;
          case rc::Edge::DEC:
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

int main(int argc, char **argv) {
  using namespace rc;
  using namespace std;
  auto gmap = restoreFromDirForDFree();
  makeDycky(gmap);
  printStatistic(gmap);

  vector<string> srcs, sinks;
  read_query_for_dfree(srcs, sinks);

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
  wpds::WPDS<Reach> P(S);

  unordered_map<string, wpds::wpds_key_t> K;

  auto start = std::chrono::high_resolution_clock::now();
  build_wpds(gmap, P);

  uint total_pairs = 0, yes_pairs = 0;
  for (auto sink : translated_query[1]) {
    auto target = make_prefix_target(sink, S);
    auto ans = wpds::poststar<Reach>(P, target, S); // compute Post*

    for (auto src : translated_query[0]) {
      ++total_pairs;
      auto &rules = ans.get_rules();
      auto it = rules.find(src);
      if (it != rules.end()) {
        for (auto &tran : it->second) {
          if (tran->transition() == A) {
            ++yes_pairs;
            break;
          }
        }
      }
    }
  }
  auto end = std::chrono::high_resolution_clock::now();
  cout << "Queries in: "
       << std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count()
       << " ms" << endl;
  cout << "total_pairs: " << total_pairs << ", yes_pairs: " << yes_pairs
       << endl;
  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  cout << "Memory usage: " << ru.ru_maxrss / 1024.0 << " MB" << endl;
  return 0;
}
