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

string getSrcFile(Instruction const *Inst) {
  auto *md = Inst->getDebugLoc().getAsMDNode();
  if (!md) {
    return "";
  }
  DILocation Loc(md);
  DIScope Scope = Loc.getScope();
  StringRef Filename = Scope.getFilename();
  StringRef Directory = Scope.getDirectory();
  return Directory.str() + "/" + Filename.str();
}

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

  string mode = "df";
  vector<string> Modules = {"netfilter", "batman"};
  unordered_map<string, unordered_set<string>> ModFuncMap;
  for (auto &F : *M) {
    if (F.isDeclaration()) {
      continue;
    }
    auto file = getSrcFile(F.getEntryBlock().getTerminator());
    for (auto &ModName : Modules) {
      if (file.find(ModName) != string::npos) {
        // outs() << ModName << " " << F.getName() << "\n";
        ModFuncMap[ModName].insert(F.getName().str());
      }
    }
  }

  ifstream out("rc_answers.txt");
  string line;
  unordered_map<string, uint> ModCntMap;
  while (getline(out, line)) {
    string func = "";
    if (mode == "ml" && line[0] != ' ') {
      func = line.substr(line.find_last_of(" ") + 1);
    } else if (mode == "df" && line[0] == ' ') {
      func = line.substr(line.find_last_of(" ") + 1);
    } else {
      continue;
    }
    func = func.substr(0, func.find("."));
    // outs() << func << "\n";
    for (auto &ModName : Modules) {
      if (ModFuncMap[ModName].find(StringRef(func).trim()) != ModFuncMap[ModName].end()) {
        ModCntMap[ModName]++;
      }
    }
  }
  for (auto &ModName : Modules) {
    fmt::print("{}: {}\n", ModName, ModCntMap[ModName]);
  }
  return 0;
}
