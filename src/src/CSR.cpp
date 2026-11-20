// borrowed from csr/main.cpp
// modified to answer p-path queries
#include "CSR.h"
#include "Grail.h"
#include "GraphUtil.h"
#include "PathTree.h"
#include "PathtreeQuery.h"
#include "Query.h"
#include "ReachBackbone.h"

#include <cassert>
#include <chrono>

const std::string indexing = "pathtree";
// const std::string indexing = "grail";
static int query_num = 100;
static int grail_dim = 2;
static string query_file;
static string graph_file;
static bool gen_query = false;
static bool read_query = false;
static int bb_epsilon = 10;
static bool transitive_closure = false;
static bool reps_tab_alg = false;

static bool timeout = false;

static double grail_index_size(Graph &ig) {
  double ret = 0;
  for (int i = 0; i < ig.num_vertices(); i++) {
    ret += sizeof(int); // ig[i].top_level
    for (int j = 0; j < ig[i].pre->size(); ++j) {
      ret += sizeof(int) * 3; // ig[i].pre[j], ig[i].middle[j], ig[i].post[j]
    }
  }
  return ret / 1024.0 / 1024.0;
}

// index the given graph for answering p-path reachability
AbstractQuery *indexFlare(Graph &vfg, GrammarTy GT, int **psccmap) {
  auto orig_vfg_size = vfg.num_vertices();
  auto orig_vfg_edges = vfg.num_edges();
  vfg.check(); // check the correctness

  auto start = std::chrono::high_resolution_clock::now();
  vfg.build_summary_edges();
  llvm::outs() << "summary edge size: " << vfg.summary_edge_size() << ' '
               << ((GT == ID_INC_ONCE || GT == ID_DEC_ONCE) ? "ID" : "CS")
               << "\n";
  auto end = std::chrono::high_resolution_clock::now();
  chrono::duration<double, std::milli> diff = end - start;
  double summary_edge_time = diff.count();
  double summary_edge_size =
      ((double)vfg.summary_edge_size() * sizeof(int) * 2 / 1024 / 1024);
  switch (GT) {
  case ID_DEC_ONCE:
    // uaf
    vfg.to_indexing_graph_for_p_path();
    break;
  case ID_INC_ONCE:
    // leak
    vfg.to_indexing_graph_for_n_path(1);
    break;
  case CS_FULL:
    // uaf
    vfg.to_indexing_graph();
    break;
  case CS_RET:
    // leak
    vfg.to_indexing_graph_for_cs_return();
    break;
  case GT_NONE:
    // no graph transformation
    break;
  }

  // merge strongly connected component
  int *sccmap =
      new int[vfg.num_vertices()]; // store pair of orignal vertex and
                                   // corresponding vertex in merged graph
  *psccmap = sccmap;
  vector<int> reverse_topo_sort;
  // cout << "Merging strongly connected component of IG ..." << endl;
  start = std::chrono::high_resolution_clock::now();
  GraphUtil::mergeSCC(vfg, sccmap, reverse_topo_sort);
  end = std::chrono::high_resolution_clock::now();
  diff = end - start;
  // cout << "Merging SCC of Indexing-Graph(IG) Duration: " << diff.count()
  //      << " ms" << endl;
  // cout << "#DAG of IG: " << vfg.num_vertices()
  //      << " #DAG of IG Edges:" << vfg.num_edges() << endl;

  // GRAIL
  Grail *grail = nullptr;
  double grail_on_ig_duration = 0;
  double grail_on_ig_size = 0;
  if (indexing == "grail" || indexing == "pathtree+grail") {
    start = std::chrono::high_resolution_clock::now();
    GraphUtil::topo_leveler(vfg);
    grail = new Grail(vfg, grail_dim, 1, false, 100);
    end = std::chrono::high_resolution_clock::now();
    diff = end - start;
    grail_on_ig_duration = diff.count();
    grail_on_ig_size = grail_index_size(vfg);
    cout << "GRAIL Indexing Construction on IG Duration: "
         << grail_on_ig_duration << " ms" << endl;
    return grail;
  }

  // PATHTREE+SCARAB
  Query *pathtree = nullptr;
  double pt_total_duration = 0;
  double pt_total_size = 0;
  if (indexing == "pathtree" || indexing == "pathtree+grail") {
    // backbone
    int epsilon = bb_epsilon;
    double pr = 0.02;
    ReachBackbone rbb(vfg, epsilon - 1, pr, 1); // NR
    rbb.setBlockNum(5);
    start = std::chrono::high_resolution_clock::now();
    rbb.backboneDiscovery(2);
    end = std::chrono::high_resolution_clock::now();
    diff = end - start;
    double bb_discover_duration = diff.count();
    pt_total_duration = bb_discover_duration;
    // cout << "Backbone Discover on IG Duration: " << bb_discover_duration
    //      << " ms" << endl;
    string filesystem = graph_file + ".backbone";
    rbb.outputBackbone(filesystem.c_str());
    int bbsize = rbb.getBBsize();
    int bbedgesize = rbb.getBBEdgesize();
    // cout << "#Backbone of IG Vertices: " << bbsize
    //      << " #Backbone of IG Edges: " << bbedgesize << endl;

    // pathtree
    int pt_alg_type = 1;
    string ggfile = filesystem + "." + to_string(epsilon) +
                    to_string((int)(pr * 1000)) + "gg";
    string labelsfile = filesystem + "." + "index";
    ifstream infile(ggfile);
    ifstream cfile;
    bool compress = false;
    if (!infile) {
      cout << "Error: Cannot open " << ggfile << endl;
      exit(-1);
    }
    Graph bbgg(infile); // backbone gated graph
    int bbggsize = bbgg.num_vertices();
    int *bbgg_sccmap =
        new int[bbggsize]; // store pair of orignal vertex and corresponding
                           // vertex in merged graph
    vector<int> bbgg_reverse_topo_sort;
    // cout << "Merging SCC of Backbone ..." << endl;

    start = std::chrono::high_resolution_clock::now();
    GraphUtil::mergeSCC(bbgg, bbgg_sccmap, bbgg_reverse_topo_sort);
    end = std::chrono::high_resolution_clock::now();
    diff = end - start;
    pt_total_duration += diff.count();
    // cout << "Merging SCC of Backbone Duration: " << diff.count() << " ms"
    //      << endl;
    // cout << "#DAG of BB Vertices: " << bbgg.num_vertices()
    //      << " #DAG of BB Edges: " << bbgg.num_edges() << endl;

    // cout << "Constructing Pathtree (PT) Indexing ..." << endl;
    start = std::chrono::high_resolution_clock::now();
    PathTree pt(bbgg, bbgg_reverse_topo_sort);
    pt.createLabels(pt_alg_type, cfile, compress);
    cout << "#Cover: " << pt.pathMap.size() << endl;
    end = std::chrono::high_resolution_clock::now();
    diff = end - start;
    auto pt_on_bb_duration = diff.count();
    pt_total_duration += diff.count();
    // cout << "#PT Indexing Construction Duration: " << pt_on_bb_duration << "
    // ms"
    //      << endl;
    ofstream lfile(labelsfile);
    pt.save_labels(lfile);

    // query utility of path tree
    double grail_on_bb_duration;
    pathtree = new PathtreeQuery(filesystem.c_str(), vfg, epsilon, pr, true,
                                 &grail_on_bb_duration);
    return pathtree;
  }
  assert(false);
}
