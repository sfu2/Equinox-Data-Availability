#include "FUtil.h"
#include "GraphWrapper.h"
#include "Spec.h"
#include "fmt/format.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <unordered_map>
#include <unordered_set>

using namespace llvm;
using namespace std;

string _getFileLine(string const &FilePath, int LineNum) {
  ifstream OS(FilePath);
  string line;
  while (getline(OS, line)) {
    if (--LineNum == 0) {
      return StringRef(line).trim().str();
    }
  }
  assert(false);
}

string getSrcLine(Instruction const *Inst) {
  auto *md = Inst->getDebugLoc().getAsMDNode();
  if (!md) {
    return "";
  }
  DILocation Loc(md);
  DIScope Scope = Loc.getScope();
  StringRef Filename = Scope.getFilename();
  StringRef Directory = Scope.getDirectory();
  std::string FullPath = Directory.str() + "/" + Filename.str();
  return _getFileLine(FullPath, Loc.getLineNumber()) +
         fmt::format(":{}", Loc.getLineNumber());
}

struct ProgramState {
  unordered_map<string, rc::Node *> NameNodeMap;
};

// a set of info used when analyzing each function
struct FunctionSmry {
  unordered_map<const BasicBlock *, rc::Node *> BBNodeMap;
  rc::Graph G;
};

void processBB(BasicBlock const &BB, FunctionSmry &FSmry) {
  // get previous nodes
  unordered_set<rc::Node *> prevNodes;
  for (auto PI = pred_begin(&BB); PI != pred_end(&BB); ++PI) {
    auto prevBB = *PI;
    auto prevNode = FSmry.BBNodeMap[prevBB];
    if (prevNode)
      prevNodes.insert(prevNode);
  }
  for (auto &I : BB) {
    rc::Node *Node = nullptr;
    if (auto CI = dyn_cast<CallInst>(&I)) {
      if (!CI->getCalledFunction() || CI->getCalledFunction()->isIntrinsic()) {
        continue;
      }
      auto calleeName = CI->getCalledFunction()->getName();
      if (!RefCntSpec::isLibFunc(calleeName)) {
        continue;
      }

      Node = new rc::Node;
      Node->Kind = match(calleeName, RefCntSpec::IncFuncs) ? rc::Node::INC
                                                           : rc::Node::DEC;
      Node->Parent = &FSmry.G;
      FSmry.G.NodeSet.insert(Node);

      auto json = new rc::json_node;
      json->site = getSrcLine(&I);
      json->kind = Node->Kind == rc::Node::INC ? "INC" : "DEC";
      Node->json = json;

    } else if (auto RI = dyn_cast<ReturnInst>(&I)) {
      Node = new rc::Node;
      Node->Kind = rc::Node::RET;
      Node->Parent = &FSmry.G;
      FSmry.G.NodeSet.insert(Node);

      auto json = new rc::json_node;
      json->site = getSrcLine(&I);
      json->kind = "RET";
      Node->json = json;

    } else {
      continue;
    }
    for (auto prevNode : prevNodes) {
      auto e = new rc::Edge;
      e->Src = prevNode;
      e->Dst = Node;
      e->connect();
    }
    prevNodes.clear();
    prevNodes.insert(Node);
  }
  if (!prevNodes.empty()) {
    FSmry.BBNodeMap[&BB] = *prevNodes.begin();
  }
}

void processFunction(Function const &F,
                     StringMap<FunctionSmry *> &FunctionSmryMap) {
  if (F.isIntrinsic() || !F.hasName() || F.isDeclaration()) {
    return;
  }
  vector<BasicBlock const *> BBs;
  assert(topsortCFG(BBs, &F, true));
  auto &FSmry = FunctionSmryMap[F.getName()];
  if (!FSmry) {
    FSmry = new FunctionSmry();
  }
  for (auto BB : BBs) {
    processBB(*BB, *FSmry);
  }
  FSmry->G.dotAll(F.getName().str() + ".dot");
}

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"

int main(int argc, char **argv) {
  if (argc != 2) {
    errs() << "usage: " << argv[0] << " <bitcode-file>\n";
    return 1;
  }

  SMDiagnostic err;
  LLVMContext Ctx;
  unique_ptr<Module> up_M = getLazyIRFileModule(argv[1], err, Ctx);
  if (!up_M) {
    err.print("", errs());
    return 1;
  }
  Module *M = up_M.get();

  // RefCntSpec::init();
  // StringMap<FunctionSmry *> FunctionSmryMap;
  // for (auto &F : *M) {
  //   processFunction(F, FunctionSmryMap);
  // }
  auto F = M->getFunction("smb2_compound_op");
  F->materialize();
  DominatorTreeWrapperPass P;
  P.runOnFunction(*F);
  llvm::LoopInfoBase<llvm::BasicBlock, llvm::Loop> KLoop;
  KLoop.Analyze(P.getDomTree());
  outs() << KLoop.end() - KLoop.begin() << "\n";

  uint e_cnt = 0, n_cnt = 0, br_cnt = 0;
  unordered_set<void *> visited;
  for (auto &BB : *F) {
    n_cnt++;
    if (auto l = KLoop.getLoopFor(&BB)) {
      visited.insert(l);
    }
    auto term = BB.getTerminator();
    if (isa<BranchInst>(term) && ((BranchInst*)term)->isConditional()) {
      br_cnt++;
    }
    for (auto _ = succ_begin(&BB); _ != succ_end(&BB); ++_) {
      e_cnt++;
    }
  }
  outs() << fmt::format("n: {}, e: {}, br: {}\n", n_cnt, e_cnt, br_cnt);
  outs() << visited.size() << "\n";

  return 0;
}

void dfs() {}
