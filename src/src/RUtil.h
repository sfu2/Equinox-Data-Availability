#ifndef RUTIL_H
#define RUTIL_H

#include "llvm/Support/raw_ostream.h"
#include "GraphWrapper.h"
#include <regex>
#include <string>

namespace rc {
using namespace std;
using namespace llvm;

template <typename T> string handleEscape(T const &Val) {
  string Str;
  raw_string_ostream OS(Str);
  OS << Val;
  OS.flush();
  Str = std::regex_replace(Str, std::regex("\""), "\\\"");
  Str = std::regex_replace(Str, std::regex("%"), "\\%");
  return Str;
}

void dumpFLARE(GraphWrapper *gw, StringRef filename);
} // namespace rc

inline std::string get_input_dir() {
  auto val = getenv("input");
  assert(val);
  return std::string(val);
}

inline std::string get_subject() {
  auto val = get_input_dir();
  return val.substr(val.find_last_of('/') + 1);
}

#endif