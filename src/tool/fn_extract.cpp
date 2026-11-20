#include "fmt/format.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <unordered_map>
#include <unordered_set>

using namespace llvm;
using namespace std;

bool isInWhiteList(StringRef const &fn_name) {
  if (fn_name.endswith("_u32") || fn_name.endswith("_u16") ||
      fn_name.endswith("_u8"))
    return true;
  if (fn_name.endswith("_le8") || fn_name.endswith("_le16") ||
      fn_name.endswith("_le32") || fn_name.endswith("_le64") ||
      fn_name.endswith("_be8") || fn_name.endswith("_be16") ||
      fn_name.endswith("_be32") || fn_name.endswith("_be64"))
    return true;
  return false;
}

namespace fmt {
template <> struct formatter<StringRef> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
  auto format(StringRef const &c, format_context &ctx) const {
    return format_to(ctx.out(), "{}", c.str());
  }
};
} // namespace fmt

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

std::string removeSuffix(StringRef Name) {
  bool hasSuffixNum = false;
  auto i = Name.size() - 1;
  for (; i >= 0; --i) {
    char ch = Name[i];
    bool isNum = ch >= '0' && ch <= '9';
    if (!hasSuffixNum && !isNum) {
      break;
    } else if (isNum && !hasSuffixNum) {
      hasSuffixNum = true;
    } else if (!isNum && hasSuffixNum) {
      if (ch == '.')
        return Name.substr(0, i);
      else
        break;
    }
  }
  return Name;
}

bool isSameType(Type *lhs, Type *rhs) {
  // outs() << *lhs << ", " << *rhs << "\n";
  if (lhs == rhs)
    return true;
  if (!lhs->isPointerTy() || !rhs->isPointerTy())
    return false;
  if (!lhs->getPointerElementType()->isStructTy() ||
      !rhs->getPointerElementType()->isStructTy())
    return false;
  auto ln = removeSuffix(lhs->getPointerElementType()->getStructName());
  auto rn = removeSuffix(rhs->getPointerElementType()->getStructName());
  return ln == rn;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    errs() << "usage: " << argv[0] << " <bitcode-file>\n";
    return 1;
  }

  SMDiagnostic err;
  LLVMContext Ctx;
  unique_ptr<Module> up_M = getLazyIRFileModule(argv[1], err, Ctx);
  Module *M = up_M.get();
  assert(M);
  assert(!verifyModule(*M, &errs()));

  StringSet putters, getters;
  for (auto &F : *M) {
    if (F.isIntrinsic() || !F.hasName() || isInWhiteList(F.getName())) {
      continue;
    }
    auto FnName = F.getName();
    // errs() << fmt::format("{}, ", FnName);
    if (FnName.find("get") != StringRef::npos) {
      getters.insert(FnName);
    } else if (FnName.find("put") != StringRef::npos) {
      putters.insert(FnName);
    }
  }

  // check for matched names
  unordered_map<string, string> matched_pairs;
  for (auto &getter : getters) {
    auto i = getter.getKey().find("get");
    auto j = i + 3;
    auto key = (getter.getKey().substr(0, i) + getter.getKey().substr(j)).str();
    for (auto &putter : putters) {
      auto i = putter.getKey().find("put");
      auto j = i + 3;
      auto pkey =
          (putter.getKey().substr(0, i) + putter.getKey().substr(j)).str();
      if (pkey == key) {
        matched_pairs.insert({getter.getKey(), putter.getKey()});
        break;
      }
    }
  }

  for (auto it = matched_pairs.begin(); it != matched_pairs.end();) {
    auto &p = *it;
    auto getter = M->getFunction(p.first);
    getter->materialize();
    for (auto &BB : *getter) {
      for (auto &II : BB) {
        // rule 1: calls another refcnt wrapper
        if (II.getOpcode() == Instruction::Call) {
          auto inner_call = ((CallInst *)(&II))->getCalledFunction();
          if (!inner_call || !inner_call->hasName()) {
            continue;
          }
          if (isInWhiteList(inner_call->getName())) {
            errs() << fmt::format("bad api: {{\"{}\", \"{}\"}},\n", p.first,
                                  p.second);
            it = matched_pairs.erase(it);
            goto bad;
          }
        }
      }
    }
    ++it;
    continue;
  bad:;
  }

  // select matched arguments
  errs() << "Matched Apis:\n";
  for (auto it = matched_pairs.begin(); it != matched_pairs.end();) {
    auto &p = *it;
    auto getter_t = M->getFunction(p.first)->getFunctionType();
    auto putter_t = M->getFunction(p.second)->getFunctionType();
    assert(getter_t && putter_t);
    // errs() << p.first << ' ' << *getter_t << ' ' << p.second << ' ' << *putter_t
    //        << '\n';

    string flag = "NONE";

    // pre-check: if operate on counter directly
    bool operate_on_counter = false;
    for (auto II : M->getFunction(p.first)->users()) {
      auto *getter_user = dyn_cast<Instruction>(II);
      if (getter_user && getter_user->getOpcode() == Instruction::Call) {
        auto CI = cast<CallInst>(getter_user);
        auto src_line = getSrcLine(CI);
        if (src_line.find("count") != string::npos ||
            src_line.find("cnt") != string::npos) {
          operate_on_counter = true;
          break;
        }
      }
    }
    if (operate_on_counter)
      flag += "|ACCEPT_REFCOUNT";
    int inc_id = -2, dec_id = -2;

    // rule 1: exact match arg 0 type && getter returns arg0
    if (isSameType(getter_t->getParamType(0), putter_t->getParamType(0)) &&
        isSameType(getter_t->getParamType(0), getter_t->getReturnType())) {
      inc_id = 0;
      dec_id = 0;
      flag += "|GETTER_RETURN_ARG";
      goto emit;
    }
    // rule 2: exact match arg 0 type && getter returns int
    else if (isSameType(getter_t->getParamType(0), putter_t->getParamType(0)) &&
             (getter_t->getReturnType()->isIntegerTy() || getter_t->getReturnType()->isPointerTy())) {
      inc_id = 0;
      dec_id = 0;
      flag += "|GETTER_RETURN_ERR_CODE";
      goto emit;
    }
    // rule 3: exact match arg 0 type && getter returns void
    else if (isSameType(getter_t->getParamType(0), putter_t->getParamType(0)) &&
             getter_t->getReturnType()->isVoidTy()) {
      inc_id = 0;
      dec_id = 0;
      goto emit;
    }
    // rule 4: getter return match setter arg
    else if (isSameType(getter_t->getReturnType(), putter_t->getParamType(0))) {
      inc_id = -1;
      dec_id = 0;
      goto emit;
    }
    ++it;
    continue;
  emit:
    errs() << fmt::format("{{\"{}\", ApiPair(\"{}\", {}, \"{}\", {}, {})}},\n",
                          p.first, p.first, inc_id, p.second, dec_id, flag);
    it = matched_pairs.erase(it);
  }

  // select matched arguments
  errs() << "\n\nUnmatched Apis:\n";
  for (auto it = matched_pairs.begin(); it != matched_pairs.end(); it++) {
    auto &p = *it;
    // errs() << fmt::format("{{\"{}\", \"{}\"}},", p.first, p.second);
    auto getter = M->getFunction(p.first);
    auto putter = M->getFunction(p.second);
    assert(getter && putter);
    errs() << p.first << "\t" << *getter->getFunctionType() << "\n";
    errs() << p.second << "\t" << *putter->getFunctionType() << "\n";
  }
  errs() << "\n";
  return 0;
}
