#ifndef GW_H
#define GW_H

#include "AbstractQuery.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct Graph;
struct Grail;
using _Graph = Graph;
enum GrammarTy { GT_NONE, CS_FULL, CS_RET, ID_INC_ONCE, ID_DEC_ONCE };

namespace rc {

using namespace std;
using namespace llvm;

struct Node;
struct Graph;
struct json_node {
  string id;
  string site;
  string node;
  string kind;
  string callee;
  vector<string> callee_args;
  vector<string> callee_rets;

  Node *into(Graph *g);
};
struct Edge;
struct Node {
  enum KindTy { NIL, ARG, CS_ARG, ARG_RET, RET, CS_RET, OOS, UNREACHABLE, INC, DEC };
  static const char *KindNameVec[];

  json_node *json = nullptr;
  unordered_set<Edge *> In, Out;
  KindTy Kind = NIL;
  Graph *Parent;
  bool cloned = false;
  Node *siblingNode = nullptr;

  void verify();

  void dot(raw_ostream &os);

  string getId() { return json->id + (cloned ? "$" : ""); }

  const char *getKindName() const { return KindNameVec[Kind]; }
};

// a multi-dyck graph
// where edges are use sites
// and nodes are all place holders
struct Graph {
  Graph(){};
  Graph(Graph const &) = delete;

  std::string fn_name;

  StringMap<Node *> NodeMap; // node hash -> node

  unordered_set<Node *> NodeSet;

  Node *getNode(string id);

  Node *getNodeIfExists(string id);

  void dotIntra(StringRef filename);

  // dot all reachable
  void dotAll(StringRef filename);

  void getOOSNodes(std::vector<Node *> &ret);

  void getRetNodes(std::vector<Node *> &ret, Node *arg);
  void verify();
};

struct Edge {
  static const char *KindNameVec[];
  enum KindTy { NIL, INC, DEC, CALL, RET };
  void const *label = nullptr;
  // CALL and RET edge of the same call site should have the same label!

  KindTy Kind = NIL;
  Node *Src = nullptr;
  Node *Dst = nullptr;

  void connect() {
    assert(Src && Dst);
    Src->Out.insert(this);
    Dst->In.insert(this);
  }

  void connectSrc() { Src->Out.insert(this); }

  void disconnectSrc() { Src->Out.erase(this); }

  void disconnectDst() { Dst->In.erase(this); }

  static const char *getKindName(KindTy K) { return KindNameVec[K]; }
  const char *getKindName() { return KindNameVec[Kind]; }

  void dot(raw_ostream &os);

  void verify();
};

struct GraphWrapper {
  _Graph *flare_graph;
  uint64_t incr_cnt = 0;
  unordered_map<Node *, uint64_t> NodeIdxMap; // Node -> Index
  vector<Node *> NodeIdxVec; // Index -> Node
  unordered_map<string, uint64_t> NodeIdToIdxMap; // Hash -> Index

  GraphWrapper(_Graph *flare_graph) : flare_graph(flare_graph) {}

  uint64_t getNodeIdxFromPtr(Node *n) {
    auto it = NodeIdxMap.find(n);
    if (it != NodeIdxMap.end()) {
      return it->second;
    }
    NodeIdxMap[n] = incr_cnt;
    NodeIdToIdxMap[n->getId()] = incr_cnt;
    NodeIdxVec.push_back(n);
    assert(NodeIdxVec.size() == incr_cnt + 1);
    return incr_cnt++;
  }

  uint64_t getNodeIdx(string hash) const {
    auto it = NodeIdToIdxMap.find(hash);
    assert(it != NodeIdToIdxMap.end());
    return it->second;
  }

  uint64_t getNodeIdx(Node *n) const {
    auto it = NodeIdxMap.find(n);
    assert(it != NodeIdxMap.end());
    return it->second;
  }

  Node *getNodeFromHash(string hash) const {
    auto idx = getNodeIdx(hash);
    return NodeIdxVec[idx];
  }

  uint64_t getNodeIdxStrict(Node *n) {
    auto it = NodeIdxMap.find(n);
    if (it != NodeIdxMap.end()) {
      return it->second;
    }
    assert(false);
    return 0;
  }

  Node *getNode(uint64_t idx) { return NodeIdxVec[idx]; }
};

struct GrailWrapper {
  rc::GraphWrapper *gw = nullptr;

  AbstractQuery *q = nullptr;
  int *sccmap = nullptr;
  size_t original_size = 0;
  GrammarTy GT;
  _Graph *g;

  void index(GrammarTy GT);

  bool fast_check(uint s, uint t);

  bool reach(uint s, uint t);

  uint getId(uint s) const;

  ~GrailWrapper() {
    delete sccmap;
    delete gw;
    delete q;
  }
};

typedef unordered_map<std::string, vector<Graph *>> GraphCollection;

struct Timer {
private:
  bool running = false;
  std::chrono::time_point<std::chrono::high_resolution_clock,
                          std::chrono::nanoseconds>
      last_tic;
  std::chrono::nanoseconds elapsed;

public:
  void tic() {
    assert(!running);
    running = true;
    last_tic = std::chrono::high_resolution_clock::now();
  }

  void toc() {
    auto now = std::chrono::high_resolution_clock::now();
    assert(running);
    running = false;
    elapsed += (now - last_tic);
  }

  long long count() {
    assert(!running);
    return chrono::duration_cast<chrono::microseconds>(elapsed).count();
  }
};

} // namespace rc

#endif
