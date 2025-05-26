#pragma once

#include "llvm/Passes/PassBuilder.h"

using namespace llvm;

namespace polaris {

struct BogusControlFlow2 : PassInfoMixin<BogusControlFlow2> {
  bool EnabledFlag;
  BogusControlFlow2() : EnabledFlag(false) {}
  explicit BogusControlFlow2(bool Enabled) : EnabledFlag(Enabled) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

}; // namespace polaris
