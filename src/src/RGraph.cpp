
#include "RGraph.h"
#include "CSR.h"
#include "Grail.h"
#include "Graph.h"
#include "GraphWrapper.h"
#include "llvm/ADT/StringSet.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <llvm/Support/raw_ostream.h>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <unordered_map>

using namespace llvm;

rc::GraphWrapper *global_gw = nullptr;

namespace rc {

void Node::dot(raw_ostream &os) {
  os << fmt::format("\t\"{:#x}\"[label=\"", uint64_t(this));
  if (json) {
    os << fmt::format("{}({})", handleEscape(json->site),
                      handleEscape(json->node));
  }
  os << "\"];\n";
}

const char *Node::KindNameVec[] = {"NIL", "ARG",    "CS_ARG", "ARG_RET",
                                   "RET", "CS_RET", "OOS",    "UNREACHABLE",
                                   "INC", "DEC"};

Node *json_node::into(Graph *g) {
  auto n = new Node;
  if (kind == "CALL_ARG") {
    n->Kind = Node::CS_ARG;
  } else if (kind == "CALL_RET") {
    n->Kind = Node::CS_RET;
  } else if (kind == "OOS") {
    n->Kind = Node::OOS;
  } else if (kind == "RET") {
    n->Kind = Node::RET;
  } else if (kind == "ARG") {
    n->Kind = Node::ARG;
  } else if (kind == "INC") {
    n->Kind = Node::INC;
  } else if (kind == "DEC") {
    n->Kind = Node::DEC;
  } else if (kind == "ARG_RET") {
    n->Kind = Node::ARG_RET;
  } else {
    assert(false);
  }
  n->json = this;
  n->Parent = g;
  g->NodeMap[id] = n;
  g->NodeSet.insert(n);
  return n;
}

struct json_edge {
  string src;
  string dst;

  void into(Graph *g) {
    auto e = new Edge;
    e->Src = g->getNode(src);
    e->Dst = g->getNode(dst);
    e->connect();
  }
};

rapidjson::Value DefaultArray(rapidjson::kArrayType);
rapidjson::Value const &getArray(rapidjson::Value &V, const char *key) {
  auto it = V.FindMember(key);
  if (it == V.MemberEnd()) {
    return DefaultArray;
  }
  return it->value;
}

string getOrDefault(rapidjson::Value &V, const char *key) {
  auto it = V.FindMember(key);
  if (it == V.MemberEnd()) {
    return "";
  }
  return it->value.GetString();
}

uint64_t getOrDefaultUint64(rapidjson::Value &V, const char *key) {
  auto it = V.FindMember(key);
  if (it == V.MemberEnd()) {
    return 0;
  }
  return it->value.GetUint64();
}

string GetJsonText(rapidjson::Value &doc) {
  rapidjson::StringBuffer buffer;
  buffer.Clear();
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);
  return string(buffer.GetString());
}

string GetString(rapidjson::Value const &Val) {
  if (Val.IsString()) {
    return Val.GetString();
  }
  return to_string(Val.GetUint64());
}

Graph *fromFile(std::string const &filename) {
  using namespace rapidjson;

  ifstream ifs(filename);
  IStreamWrapper isw(ifs);
  Document d;
  d.ParseStream(isw);

  auto g = new Graph();
  g->fn_name = d["fn"].GetString();
  // we convert a ref graph node into two nodes and one edge, to make it more
  // dyck-cfl
  auto &nodes = d["nodes"];
  for (auto ItNode = nodes.Begin(); ItNode != nodes.End(); ++ItNode) {
    auto &node = *ItNode;
    auto jnp = new json_node;
    auto &jn = *jnp;
    // errs() << GetJsonText(node) << "\n";
    jn.id = GetString(node["id"]);
    jn.site = node["site"].GetString();
    jn.node = node["node"].GetString();
    jn.kind = node["kind"].GetString();
    jn.callee = getOrDefault(node, "callee");
    auto &callee_args = getArray(node, "callee_args");
    for (auto ItArg = callee_args.Begin(); ItArg != callee_args.End();
         ++ItArg) {
      jn.callee_args.push_back(GetString(*ItArg));
    }
    auto &callee_rets = getArray(node, "callee_rets");
    for (auto ItArg = callee_rets.Begin(); ItArg != callee_rets.End();
         ++ItArg) {
      jn.callee_rets.push_back(GetString(*ItArg));
    }
    jn.into(g);
  }
  auto &edges = d["edges"];
  for (auto ItEdge = edges.Begin(); ItEdge != edges.End(); ++ItEdge) {
    auto &edge = *ItEdge;
    json_edge en;
    en.src = GetString(edge["src"]);
    en.dst = GetString(edge["dst"]);
    en.into(g);
  }
  return g;
}

Node *getNodeInVec(vector<Graph *> const &vec, string &hash) {
  Node *n = nullptr;
  for (auto callee_g : vec) {
    n = callee_g->getNodeIfExists(hash);
    if (n)
      return n;
  }
  assert(false);
}

GraphCollection restoreFromDir(string path) {
  GraphCollection gmap;
  // load from file
  for (const auto &entry : std::filesystem::directory_iterator(path)) {
    if (entry.is_directory()) {
      continue;
    }
    assert(entry.path().extension() == ".json");
    auto g = fromFile(entry.path());
    g->verify();
    gmap[g->fn_name].push_back(g);
  }
  // inter connect
  for (auto &item : gmap) {
    auto &gs = item.second;
    for (auto g : gs) {
      for (auto &node_pair : g->NodeMap) {
        auto n = node_pair.second;
        for (auto &callee_arg_id : n->json->callee_args) {
          auto &callee_gs = gmap[n->json->callee];
          if (callee_gs.empty())
            continue;
          Node *callee_arg = getNodeInVec(callee_gs, callee_arg_id);
          auto e = new Edge;
          e->Src = n;
          e->Dst = callee_arg;
          e->Kind = Edge::CALL;
          e->label = e;
          e->connect();
        }
        for (auto &callee_ret_id : n->json->callee_rets) {
          auto &callee_gs = gmap[n->json->callee];
          if (callee_gs.empty())
            continue;
          Node *callee_ret = getNodeInVec(callee_gs, callee_ret_id);
          auto e = new Edge;
          e->Src = callee_ret;
          e->Dst = n;
          e->label = e;
          e->Kind = Edge::RET;
          e->connect();
        }
      }
      g->verify();
    }
  }
  return gmap;
}

GraphCollection restoreFromDirForDFree() {
  return restoreFromDir(get_input_dir() + "_nps");
}

GraphCollection restoreFromDirForLeak() {
  return restoreFromDir(get_input_dir());
}

GraphCollection restoreFromDirForDFree(std::string input_dir) {
  return restoreFromDir(input_dir + "_nps");
}

GraphCollection restoreFromDirForLeak(std::string input_dir) {
  return restoreFromDir(input_dir);
}

const char *Edge::KindNameVec[] = {"NIL", "INC", "DEC", "CALL", "RET"};

void Edge::dot(raw_ostream &os) {
  os << fmt::format("\t\"{:#x}\" -> \"{:#x}\"[label=\"", (uint64_t)Src,
                    (uint64_t)Dst);
  if (Kind == CALL || Kind == RET) {
    os << fmt::format("{}({})", getKindName(Kind), label);
  }
  os << "\"];\n";
}

void Edge::verify() {
  assert(Src->Out.count(this));
  assert(Dst->In.count(this));
  assert((Kind == RET || Kind == CALL) || (Src->Parent == Dst->Parent));
  assert(!(Kind == RET || Kind == CALL) || (Src->Parent != Dst->Parent));
}

void Node::verify() {
  assert(Parent);
  for (auto e : Out) {
    assert(e->Src == this);
    e->verify();
  }
  for (auto e : In) {
    assert(e->Dst == this);
    e->verify();
  }
}

void makeDycky(GraphCollection &gmap) {
  for (auto &item : gmap) {
    auto &gs = item.second;
    // transform call/ret
    for (auto g : gs) {
      vector<Node *> call_nodes;
      for (auto n : g->NodeSet) {
        if (n->json->kind == "CALL_ARG") {
          call_nodes.push_back(n);
        }
      }
      for (auto n : call_nodes) {
        auto call_ret_node = new Node;
        call_ret_node->json = n->json;
        call_ret_node->Parent = g;
        call_ret_node->Kind = Node::CS_RET;
        call_ret_node->cloned = true;
        assert(!n->siblingNode && !n->cloned);
        n->siblingNode = call_ret_node;
        call_ret_node->siblingNode = n;
        g->NodeSet.insert(call_ret_node);
        // split call site node
        vector<Edge *> call_edge;
        vector<Edge *> intra_out_edges;
        for (auto it = n->Out.begin(); it != n->Out.end();) {
          auto e = *it;
          if (e->Kind != Edge::CALL) {
            intra_out_edges.push_back(e);
            it = n->Out.erase(it);
          } else {
            call_edge.push_back(e);
            assert(e->label == e);
            ++it;
          }
        }
        for (auto e : intra_out_edges) {
          e->Src = call_ret_node;
          e->connectSrc();
        }
        for (auto const *e : call_edge) {
          auto callee_g = e->Dst->Parent;
          vector<Node *> callee_exit_nodes;
          callee_g->getRetNodes(callee_exit_nodes, e->Dst);
          // assert(!callee_exit_nodes.empty());
          // callee_g->getOOSNodes(callee_exit_nodes);
          // we do not need oos for inter-procedural
          for (auto exit_node : callee_exit_nodes) {
            auto ret_edge = new Edge;
            ret_edge->Src = exit_node;
            ret_edge->Dst = call_ret_node;
            ret_edge->Kind = Edge::RET;
            ret_edge->label = e;
            assert(e->Src->json == call_ret_node->json);
            ret_edge->connect();
          }
        }
        // auto ne = new Edge;
        // ne->Src = n;
        // ne->Dst = call_ret_node;
        // ne->connect();
      }
      g->verify();

      // transform inc/dec
      vector<Node *> rc_nodes;
      for (auto n : g->NodeSet) {
        if (n->Kind == Node::INC || n->Kind == Node::DEC) {
          rc_nodes.push_back(n);
        }
      }
      for (auto n : rc_nodes) {
        auto nn = new Node;
        nn->Parent = g;
        nn->json = n->json;
        nn->cloned = true;
        g->NodeSet.insert(nn);
        vector<Edge *> out_edges(n->Out.begin(), n->Out.end());
        n->Out.clear();
        for (auto e : out_edges) {
          e->Src = nn;
          e->connectSrc();
        }
        auto ne = new Edge;
        ne->Src = n;
        ne->Dst = nn;
        ne->Kind = n->json->kind == "INC" ? Edge::INC : Edge::DEC;
        ne->connect();
      }
      g->verify();
    }
  }
}

void printStatistic(GraphCollection const &gmap) {
  uint inc = 0, dec = 0, call = 0, ret = 0;
  uint n_cnt = 0, e_cnt = 0;
  for (auto &p : gmap) {
    for (auto g : p.second) {
      for (auto n : g->NodeSet) {
        n_cnt++;
        for (auto e : n->Out) {
          ++e_cnt;
          switch (e->Kind) {
          case Edge::INC:
            ++inc;
            break;
          case Edge::DEC:
            ++dec;
            break;
          case Edge::CALL:
            ++call;
            break;
          case Edge::RET:
            ++ret;
            break;
          case Edge::NIL:
            break;
          }
        }
      }
    }
  }
  outs() << fmt::format("==============================================\n"
                        "General Statistic:\n"
                        "#Node: {}\t#Edge: {}\n"
                        "Label Statistic:\n"
                        "INC: {}\tDEC: {}\tCALL: {}\tRET: {}\n"
                        "INC/DEC: {}\tCALL/RET: {}\n"
                        "==============================================\n",
                        n_cnt, e_cnt, inc, dec, call, ret, inc + dec,
                        call + ret);
}

GraphWrapper *convertForFLARE(GraphCollection &gmap, DyckType dt) {
  auto gw = new GraphWrapper{new _Graph};
  bool is_first = true;
  if (global_gw) {
    is_first = false;
    gw->NodeIdxMap = global_gw->NodeIdxMap;
    gw->incr_cnt = global_gw->incr_cnt;
    gw->NodeIdxVec = global_gw->NodeIdxVec;
  } else {
    global_gw = gw;
  }
  size_t n = [&] {
    size_t cnt = 0;
    for (auto &p : gmap) {
      for (auto g : p.second) {
        cnt += g->NodeSet.size();
      }
    }
    return cnt;
  }();
  auto &fg = *gw->flare_graph;
  fg.n_vertices = n;
  fg.vl = VertexList(n);
  fg.graph = GRA(n, In_OutList());

  for (int i = 0; i < n; i++) {
    fg.addVertex(i);
  }

  for (auto &p : gmap) {
    for (auto g : p.second) {
      for (auto n : g->NodeSet) {
        auto idx =
            is_first ? gw->getNodeIdxFromPtr(n) : gw->getNodeIdxStrict(n);
        for (auto e : n->Out) {
          auto tidx = is_first ? gw->getNodeIdxFromPtr(e->Dst)
                               : gw->getNodeIdxStrict(e->Dst);
          int64_t label = 0;
          switch (e->Kind) {
          case Edge::INC:
            if (dt == ID)
              label = 1;
            break;
          case Edge::DEC:
            if (dt == ID)
              label = -1;
            break;
          case Edge::CALL:
            assert(e->label);
            if (dt == CS)
              label = (int64_t)(e->label);
            break;
          case Edge::RET:
            assert(e->label);
            if (dt == CS)
              label = -(int64_t)(e->label);
            break;
          case Edge::NIL:
            break;
          }
          if (label)
            fg.addEdge(idx, tidx, label);
          else
            fg.addEdge(idx, tidx);
        }
      }
    }
  }
  assert(gw->incr_cnt == gw->flare_graph->vl.size());
  assert(gw->incr_cnt == global_gw->incr_cnt);
  return gw;
}

void findReachable(Node *n, vector<vector<Edge *> *> &reachable_traces) {
  stack<Node *> worklist;
  unordered_set<Node *> visited;
  unordered_map<Node *, Edge *> track;
  worklist.push(n);
  visited.insert(n);
  track[n] = nullptr;
  while (!worklist.empty()) { // dfs
    auto n = worklist.top();
    worklist.pop();
    for (auto e : n->Out) {
      auto nn = e->Dst;
      if (e->Kind == Edge::INC || e->Kind == Edge::DEC) {
        auto trace = new vector<Edge *>;
        auto pe = e;
        while (pe != nullptr) {
          trace->push_back(pe);
          pe = track[e->Src];
        }
        reachable_traces.push_back(trace);
        visited.insert(nn);
      } else {
        if (visited.insert(nn).second) {
          // not visited
          track[nn] = e;
          worklist.push(nn);
        }
      }
    }
  }
}

void dumpGraph(GraphCollection const &gmap, StringRef filename) {
  std::error_code ec;
  raw_fd_ostream out(filename, ec, llvm::sys::fs::F_Text);
  out << "digraph G{\n";
  for (auto &p : gmap) {
    for (auto g : p.second) {
      for (auto n : g->NodeSet) {
        out << fmt::format("\ta{0}[label=\"{0} {1} {2}\"];\n", n->getId(),
                           n->json->site, n->Parent->fn_name);
        for (auto e : n->Out) {
          auto t = e->Dst;
          out << fmt::format("\ta{} -> a{}", n->getId(), t->getId());
          if (e->Kind != Edge::NIL)
            out << fmt::format("[label=\"{}\"]", e->getKindName());
          out << ";\n";
        }
      }
    }
  }
  out << "}";
}

void dumpGraph(GraphCollection const &gmap, StringRef filename,
               StringSet<> const &fnames) {
  std::error_code ec;
  raw_fd_ostream out(filename, ec, llvm::sys::fs::F_Text);
  out << "digraph G{\n";
  for (auto &p : gmap) {
    for (auto g : p.second) {
      for (auto n : g->NodeSet) {
        if (fnames.count(n->Parent->fn_name)) {
          out << fmt::format("\ta{0}[label=\"{3} {1} {2}\"];\n", n->getId(),
                             n->json->site, n->Parent->fn_name,
                             n->getKindName());

          for (auto e : n->Out) {
            auto t = e->Dst;
            if (fnames.count(t->Parent->fn_name)) {
              out << fmt::format("\ta{} -> a{}", n->getId(), t->getId());
              if (e->Kind != Edge::NIL)
                out << fmt::format("[label=\"{}\"]", e->getKindName());
              out << ";\n";
            }
          }
        }
      }
    }
  }
  out << "}";
}

Node *Graph::getNode(string hash) {
  auto n = getNodeIfExists(hash);
  assert(n);
  return n;
}

Node *Graph::getNodeIfExists(string hash) {
  auto it = NodeMap.find(hash);
  if (it != NodeMap.end()) {
    return it->second;
  }
  return nullptr;
}

void Graph::dotIntra(StringRef filename) {
  std::error_code ec;
  raw_fd_ostream out(filename, ec, sys::fs::F_RW | sys::fs::F_Text);
  out << "digraph G {\n";
  for (auto n : NodeSet) {
    n->dot(out);
    for (auto e : n->Out) {
      e->dot(out);
    }
  }
  out << "}\n";
}

// dot all reachable
void Graph::dotAll(StringRef filename) {
  std::error_code ec;
  raw_fd_ostream out(filename, ec, sys::fs::F_RW | sys::fs::F_Text);
  out << "digraph G {\n";
  stack<Node *> worklist;
  unordered_set<Node *> visited;
  for (auto n : NodeSet) {
    worklist.push(n);
    visited.insert(n);
  }
  while (!worklist.empty()) {
    auto n = worklist.top();
    worklist.pop();
    n->dot(out);
    for (auto e : n->Out) {
      if (e->Src->Parent == this && e->Kind == Edge::RET) {
        continue;
      }
      e->dot(out);
      if (visited.insert(e->Dst).second)
        worklist.push(e->Dst);
    }
  }
  out << "}\n";
}

void Graph::getOOSNodes(std::vector<Node *> &ret) {
  for (auto &p : NodeMap) {
    auto n = p.second;
    if (n->json->kind == json_kind::OOS) {
      ret.push_back(n);
    }
  }
}

void Graph::getRetNodes(std::vector<Node *> &ret, Node *arg) {
  vector<Node *> worklist;
  unordered_set<Node *> visited;
  worklist.push_back(arg);
  visited.insert(arg);
  // outs() << "------------------\n";
  while (!worklist.empty()) {
    auto n = worklist.back();
    // outs() << n->getId() << "\n";
    worklist.pop_back();
    if (n->json->kind == json_kind::RET ||
        n->json->kind == json_kind::ARG_RET) {
      ret.push_back(n);
      continue;
    }
    if (n->Kind == Node::CS_ARG && n->siblingNode) {
      if (visited.insert(n->siblingNode).second)
        worklist.push_back(n->siblingNode);
    }
    for (auto e : n->Out) {
      auto nn = e->Dst;
      if (nn->Parent != arg->Parent) {
        continue;
      }
      if (visited.insert(nn).second)
        worklist.push_back(nn);
    }
  }
}

void Graph::verify() {
  for (auto n : NodeSet) {
    n->verify();
  }
}

void GrailWrapper::index(GrammarTy GT) {
  g = gw->flare_graph;
  original_size = g->num_vertices();
  q = indexFlare(*gw->flare_graph, GT, &sccmap);
  this->GT = GT;
}

bool GrailWrapper::fast_check(uint s, uint t) { return false; }

bool GrailWrapper::reach(uint s, uint t) {
  if (GT == ID_INC_ONCE || GT == ID_DEC_ONCE)
    t += original_size;

  return q->reach(sccmap[s], sccmap[t]);
  // return q->reachPP_lf(s, t);
}

uint GrailWrapper::getId(uint s) const { return sccmap[s]; }

} // namespace rc
