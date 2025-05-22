#pragma once

#include "llvm/Passes/PassBuilder.h"

using namespace llvm;

namespace polaris {
struct IndirectBlockInfo {
  BasicBlock *BB;
  unsigned IndexWithinTable;
  unsigned RandomKey;
};
struct IndirectBranch : PassInfoMixin<IndirectBranch> {
  bool EnabledFlag;
  IndirectBranch() : EnabledFlag(false) {}
  explicit IndirectBranch(bool Enabled) : EnabledFlag(Enabled) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  void process(Function &F);
  static bool isRequired() { return true; }
};

}; // namespace polaris