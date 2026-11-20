#include "GraphWrapper.h"
#include "fmt/format.h"
#include <climits>
#include <list>
#include <unordered_map>
#include <vector>

namespace rc {

using namespace std;

struct Facts {
  unordered_set<int> vals;
  static const uint K = 5;
  static const int UPPER_BOUND = K, LOWER_BOUND = -K;

  Facts() { (void)&Facts::dump; }
  Facts(Facts const &a) : vals(a.vals) {}
  Facts(initializer_list<int> &&vals) : vals(vals) {}
  Facts(unordered_set<int> &&vals) : vals(vals) {}

  bool changed = false;
  void reset() { changed = false; }

  void dump() const {
    outs() << "{ ";
    for (auto val : vals) {
      outs() << val << " ";
    }
    outs() << "}";
  }

  static Facts merge(Facts const &a, Facts const &b) {
    Facts facts;
    facts.merge(a);
    facts.merge(b);
    return facts;
  }

  // set join
  void merge(Facts const &a) { 
    uint size = vals.size();
    vals.insert(a.vals.begin(), a.vals.end()); 
    changed = (size != vals.size());
  }

  static int add(int a, int b) {
    if (a == INT_MIN || b == INT_MIN) {
      return INT_MIN;
    } else if (a == INT_MAX || b == INT_MAX) {
      return INT_MAX;
    }
    auto ret = a + b;
    if (ret > UPPER_BOUND)
      return INT_MAX;
    else if (ret < LOWER_BOUND)
      return INT_MIN;
    else
      return ret;
  }

  // apply
  Facts apply(Facts const &a) const {
    Facts facts;
    auto &new_vals = facts.vals;
    for (auto val : vals) {
      for (auto a_val : a.vals) {
        new_vals.insert(add(val, a_val));
      }
    }
    return facts;
  }

  Facts inc() const {
    Facts facts;
    auto &new_vals = facts.vals;
    for (int val : vals) {
      new_vals.insert(add(val, 1));
    }
    return facts;
  }

  Facts dec() const {
    Facts facts;
    auto &new_vals = facts.vals;
    for (int val : vals) {
      new_vals.insert(add(val, -1));
    }
    return facts;
  }
};

struct FSummary {
  // symbolic: ignore same node
  unordered_set<Node *> visitedSrc;
  // symbolic smry: src -> <sink, facts>
  unordered_map<Node *, unordered_map<Node *, Facts const *>> symbolicSmry;
  // intra dataflow fact of exit nodes
  unordered_map<Node *, Facts *> concreteFacts;
};

struct IdeSolver {
  GraphCollection *gmap;

  IdeSolver(GraphCollection *gmap) : gmap(gmap) {}

  void index(Node *src) {
    for (auto &kv : FunctionSummaries) {
      auto &f_smry = kv.second;
      for (auto &kv2 : f_smry.concreteFacts) {
        delete kv2.second;
      }
      f_smry.concreteFacts.clear();
    }
    track(src, false);
  }

  // return true if
  // - src-sink is context-sensitive reachable
  // - counter > 0
  // everything is computed on-demand
  bool reach(Node *src, Node *sink) {
    auto sink_fn = sink->Parent->fn_name;
    auto sink_facts = FunctionSummaries[sink_fn].concreteFacts[sink];
    if (!sink_facts) {
      return false;
    }
    for (auto val : sink_facts->vals) {
      if (val > 0)
        return true;
    }
    return false;
  }

private:
  unordered_map<string, FSummary> FunctionSummaries;

#define IS_DEBUG_FN(node) ((node)->Parent->fn_name == "irdma_process_pkt")

  // each track runs intra-dataflow analysis
  void track(Node *src, bool symbolic = false) {
    auto &f_smry = FunctionSummaries[src->Parent->fn_name];
    if (symbolic && !f_smry.visitedSrc.insert(src).second)
      return;

    list<Node *> worklist, retlist;
    unordered_map<Node *, Facts *> NodeFactMap;
    auto getOrCreateLocalFacts = [&NodeFactMap](Node *node) -> Facts * {
      auto f = NodeFactMap[node];
      if (!f)
        return new Facts();
      else
        return new Facts(*f);
    };
    worklist.push_back(src);
    NodeFactMap[src] = new Facts({0});
    while (!worklist.empty()) {
      auto curr_node = worklist.front();
      worklist.pop_front();

      auto facts = getOrCreateLocalFacts(curr_node);
      facts->reset();
      for (auto edge : curr_node->In) {
        if (edge->Src->Parent != curr_node->Parent)
          continue;
        auto it = NodeFactMap.find(edge->Src);
        if (it == NodeFactMap.end())
          continue;
        auto prev_fact = it->second;
        assert(prev_fact);

        switch (edge->Kind) {
        case Edge::CALL:
        case Edge::RET:
          assert(false);
          break;
        case Edge::INC:
          facts->merge(prev_fact->inc());
          break;
        case Edge::DEC:
          facts->merge(prev_fact->dec());
          break;
        default:
          facts->merge(*prev_fact);
          break;
        }
      }
      if (!facts->changed) {
        delete facts;
        continue;
      }

      if (true) {
        outs() << fmt::format("FACT {} ", curr_node->getId());
        facts->dump();
        outs() << fmt::format(" {} {}\n", curr_node->json->site,
                              curr_node->getKindName());
      }
      NodeFactMap[curr_node] = facts;
      // handle curr_node
      if (curr_node->Kind == Node::CS_ARG) {
        // process callee
        if (curr_node->Out.size() != 1) {
          outs() << (curr_node->getId()) << '\n';
        }
        assert(curr_node->Out.size() == 1);
        auto call_edge = *curr_node->Out.begin();
        assert(call_edge->Kind == Edge::CALL);
        auto callee_arg_node = call_edge->Dst;
        auto &callee_name = callee_arg_node->Parent->fn_name;
        auto cs_ret_node = curr_node->siblingNode;
        if (callee_name != curr_node->Parent->fn_name) {
          assert(cs_ret_node->Kind == Node::CS_RET);
          auto cs_arg_facts = facts;
          track(callee_arg_node, true);

          auto new_facts = new Facts();
          for (auto &call_ret_edge : cs_ret_node->In) {
            if (call_ret_edge->label == call_edge->label) {
              auto callee_smry =
                  FunctionSummaries[callee_name]
                      .symbolicSmry[callee_arg_node][call_ret_edge->Src];
              assert(callee_smry);
              new_facts->merge(cs_arg_facts->apply(*callee_smry));
            }
          }
          NodeFactMap[cs_ret_node] = new_facts;
        } else {
          NodeFactMap[cs_ret_node] = new Facts(*facts);
        }
        curr_node = cs_ret_node;
      } else if (curr_node->Kind == Node::RET || curr_node->Kind == Node::OOS ||
                 curr_node->Kind == Node::ARG_RET) {
        if (symbolic) {
          f_smry.symbolicSmry[src][curr_node] = new Facts(*facts);
        } else {
          f_smry.concreteFacts[curr_node] = new Facts(*facts);
        }
        if (curr_node->Kind == Node::RET || curr_node->Kind == Node::ARG_RET) {
          retlist.push_back(curr_node);
        }
        continue;
      }
      for (auto edge : curr_node->Out) {
        auto dst = edge->Dst;
        worklist.push_back(dst);
      }
    }
    // clean up
    for (auto &kv : NodeFactMap) {
      delete kv.second;
    }
    if (symbolic) {
      // symbolic do not care about inter-procedural
      return;
    }
    // handle return to caller
    while (!retlist.empty()) {
      auto ret_node = retlist.front();
      retlist.pop_front();
      auto ret_node_concrete_fact = getOrCreateConcreteFacts(ret_node);

      if (IS_DEBUG_FN(ret_node)) {
        outs() << fmt::format("FACT {} ", ret_node->getId());
        ret_node_concrete_fact->dump();
        outs() << fmt::format(" {} {}\n", ret_node->json->site,
                              ret_node->getKindName());
      }
      for (auto edge : ret_node->Out) {
        auto cs_out_node = edge->Dst;
        assert(cs_out_node->Kind == Node::CS_RET);
        track(cs_out_node, true);
        auto &caller_name = cs_out_node->Parent->fn_name;
        auto &caller_smry = getSymbolicFacts(cs_out_node);
        if (caller_smry.empty()) {
          continue;
        }

        for (auto &kv : caller_smry) {
          auto caller_exit_node = kv.first;
          auto caller_symbolic_facts = kv.second;
          auto caller_concrete_exit_facts =
              getOrCreateConcreteFacts(caller_exit_node);
          caller_concrete_exit_facts->merge(
              ret_node_concrete_fact->apply(*caller_symbolic_facts));

          if (caller_exit_node->Kind == Node::RET || caller_exit_node->Kind == Node::ARG_RET) {
            retlist.push_back(caller_exit_node);
          }
        }
      }
    }
  }

  Facts *getOrCreateConcreteFacts(Node *node) {
    auto &ret = FunctionSummaries[node->Parent->fn_name].concreteFacts[node];
    if (!ret)
      ret = new Facts();
    return ret;
  }

  unordered_map<Node *, Facts const *> const &getSymbolicFacts(Node *node) {
    return FunctionSummaries[node->Parent->fn_name].symbolicSmry[node];
  }
};

} // namespace rc
