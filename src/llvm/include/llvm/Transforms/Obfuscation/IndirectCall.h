#pragma once

#include "llvm/Passes/PassBuilder.h"

using namespace llvm;

namespace polaris {
struct IndirectCall : PassInfoMixin<IndirectCall> {
  bool EnabledFlag;
  IndirectCall() : EnabledFlag(false) {}
  explicit IndirectCall(bool Enabled) : EnabledFlag(Enabled) {}
  PreservedAnalyses run(Function &M, FunctionAnalysisManager &AM);
  void process(Function &F);
  static bool isRequired() { return true; }
};

}; // namespace polaris