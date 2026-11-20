#include "FUtil.h"
#include "RGraph.h"
#include "WPDS.h"
#include "ref_ptr.h"
#include <bits/stdc++.h>
#include <cassert>
using namespace std;

// ---------- Boolean Reach semiring (WPDS++ manual §2.8) ----------
class Reach {
  bool _one = false;
  set<wpds_key_t> sources;

  Reach(bool _one) : _one(_one), count(0) {}

public:
  ref_ptr<Reach>::count_t count;
  Reach() : count(0) {}
  Reach(const Reach &r) = default;
  Reach(wpds_key_t src) : count(0) { sources.insert(src); }
  Reach *quasiOne() const { return new Reach(true); }
  static Reach *one() { return new Reach(true); }
  static Reach *zero() { return new Reach(false); }
  Reach *extend(Reach *rhs) const {
    if (_one) {
      return new Reach(*rhs);
    } else if (rhs->_one) {
      return new Reach(*this);
    }
    auto r = new Reach();
    std::set_intersection(this->sources.cbegin(), this->sources.cend(),
                          rhs->sources.cbegin(), rhs->sources.cend(),
                          std::inserter(r->sources, r->sources.begin()));
    return r;
  } // ⊗
  Reach *combine(Reach *rhs) const {
    auto r = new Reach();
    std::set_union(this->sources.cbegin(), this->sources.cend(),
                   rhs->sources.cbegin(), rhs->sources.cend(),
                   std::inserter(r->sources, r->sources.begin()));
    return r;
  } // ⊕
  bool equal(Reach *rhs) const {
    if (_one && rhs->_one) {
      return true;
    } else if (_one || rhs->_one) {
      return false;
    } else if (sources.size() != rhs->sources.size()) {
      return false;
    }
    for (auto it = sources.begin(), rit = rhs->sources.begin();
         it != sources.end(); ++it) {
      if (*it != *rit) {
        return false;
      }
    }
    return true;
  }
  std::ostream &print(std::ostream &o) const { return o; }
  bool has(wpds_key_t src) const { return sources.find(src) != sources.end(); }
};

wpds::wpds_key_t A = str2key("A"), Z = str2key("Z"), ACC;
vector<wpds_key_t> alphabet;

// Build the WPDS from a labeled graph. One stack symbol A for '('; Z is bottom.
static void build_wpds(rc::GraphCollection &gmap, wpds::WPDS<Reach> &PDS) {
  alphabet = {A, Z};

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
  build_wpds(gmap, P);
  ACC = str2key("ACC_PFX");
  wpds::CA<Reach> target(S);
  target.add_final_state(ACC);
  for (auto &src : translated_query[0]) {
    target.add_initial_state(src);
    target.add(src, Z, ACC, new Reach(src));
  }
  auto ans = wpds::poststar(P, target, S);
  ans.path_summary();
  auto &rules = ans.get_rules();
  
  outs() << fmt::format("ans: {}", rules.size()) << '\n';

  // query
  uint total_pairs = 0, yes_pairs = 0;
  for (auto sink : translated_query[1]) {
    auto it = rules.find(sink);
    if (it == rules.end())
      continue;
    for (auto src : translated_query[0]) {
      ++total_pairs;
      for (auto &tran : it->second) {
        if (tran->transition() == A && tran->weight()->has(src)) {
          ++yes_pairs;
          break;
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
