#ifndef RGRAPH_H
#define RGRAPH_H

#include "GraphWrapper.h"
#include "RUtil.h"
#include "fmt/format.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSet.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;
using namespace llvm;

namespace rc {

#define KIND_KEYWORD(X) const std::string X = #X
namespace json_kind {
KIND_KEYWORD(OOS);
KIND_KEYWORD(RET);
KIND_KEYWORD(ARG_RET);
}; // namespace json_kind

struct Graph;

struct Edge;

GraphCollection restoreFromDirForDFree();
GraphCollection restoreFromDirForLeak();
GraphCollection restoreFromDirForDFree(std::string input_dir);
GraphCollection restoreFromDirForLeak(std::string input_dir);

void makeDycky(GraphCollection &gmap);

void printStatistic(GraphCollection const &gmap);

void computeIDD(GraphCollection const &gmap);

enum DyckType { DT_NONE, CS, ID };

GraphWrapper *convertForFLARE(GraphCollection &gmap, DyckType dt);

void dumpGraph(GraphCollection const &gmap, StringRef filename);

void dumpGraph(GraphCollection const &gmap, StringRef filename,
               StringSet<> const &fnames);

} // namespace rc

#define LOOP(X) for (uint __i = (X); __i > 0; --__i)

#endif
