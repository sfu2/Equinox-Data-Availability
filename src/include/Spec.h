#ifndef SPEC_H
#define SPEC_H
// Spec here does not involve with external spec files.
// Just a simple hard-coded spec for TSA analysis.

#include "llvm/ADT/StringRef.h"
#include <string>
#include <unordered_set>

#define str_set std::unordered_set<std::string>
bool match(llvm::StringRef FuncName, str_set &SpecFuncs);

namespace RefCntSpec {
extern str_set IncFuncs, DecFuncs;
void init();
inline bool isLibFunc(llvm::StringRef FuncName) {
  return match(FuncName, IncFuncs) || match(FuncName, DecFuncs);
}
} // namespace RefCntSpec

#endif /* SPEC_H */
