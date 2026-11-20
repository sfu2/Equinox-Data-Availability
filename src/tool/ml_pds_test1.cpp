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
  Reach(bool b) : isreached(b), count(1) {}
  static Reach *one() { return new Reach(true); }
  static Reach *zero() { return new Reach(false); }
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

wpds::wpds_key_t A = str2key("A"), Z = str2key("Z"), ACC;
vector<wpds_key_t> alphabet;

// Build the WPDS from a labeled graph. One stack symbol A for '('; Z is bottom.
static void build_wpds(rc::GraphCollection &gmap, wpds::WPDS<Reach> &PDS) {

  Reach *one = Reach::one();

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
  Reach *reachOne = Reach::one();
  wpds::Semiring<Reach> S(reachOne);
  wpds::WPDS<Reach> P(S);

  std::chrono::high_resolution_clock::time_point tic, toc;

  tic = std::chrono::high_resolution_clock::now();
  // index
  alphabet = {A};
  auto ROOT = str2key("ROOT");
  for (auto src : translated_query[0]) {
    alphabet.push_back(src);
  }
  build_wpds(gmap, P);
  for (auto &src : translated_query[0]) {
    P.add_rule(ROOT, Z, src, src, Z, Reach::one()); // push  (1-symbol RHS)
  }

  wpds::CA<Reach> target(S);
  ACC = str2key("ACC_PFX");
  target.add_final_state(ACC);
  target.add_initial_state(ROOT);
  target.add(ROOT, Z, ACC, Reach::one());
  auto ans = wpds::poststar(P, target, S);
  auto &rules = ans.get_rules();
  outs() << fmt::format("ans: {}", rules.size()) << '\n';

  // query
  uint total_pairs = 0, yes_pairs = 0;
  for (auto &src : translated_query[0]) {
    for (auto sink : translated_query[1]) {
      ++total_pairs;
      auto &rules = ans.get_rules();
      auto it = rules.find(sink);
      if (it == rules.end()) {
        continue;
      }
      vector<ref_ptr<wpds::CATransition<Reach>>> worklist;
      unordered_set<wpds_key_t> visited({sink});
      for (auto &p : it->second) {
        worklist.push_back(p);
      }
      while (worklist.size()) {
        auto X = worklist.back();
        worklist.pop_back();
        if (X->transition() == src) {
          ++yes_pairs;
          break;
        }
        if (!visited.insert(X->to_state()).second)
          continue;
        auto it = rules.find(X->to_state());
        if (it == rules.end()) {
          continue;
        }
        for (auto &p : it->second) {
          worklist.push_back(p);
        }
      }
    }
  }
  toc = std::chrono::high_resolution_clock::now();
  outs() << fmt::format(
      "time: {} ms\n",
      chrono::duration_cast<chrono::milliseconds>(toc - tic).count());
  cout << "total_pairs: " << total_pairs << ", yes_pairs: " << yes_pairs
       << endl;
  return 0;
}
