#pragma once

#include "llvm/Passes/PassBuilder.h"

using namespace llvm;

namespace polaris {

struct GlobalsEncryption : PassInfoMixin<GlobalsEncryption> {
  bool EnabledFlag;
  GlobalsEncryption() : EnabledFlag(false) {}
  explicit GlobalsEncryption(bool Enabled) : EnabledFlag(Enabled) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  Function *buildDecryptFunction(Module &M);
  void process(Module &M);
  static bool isRequired() { return true; }
};

}; // namespace polaris