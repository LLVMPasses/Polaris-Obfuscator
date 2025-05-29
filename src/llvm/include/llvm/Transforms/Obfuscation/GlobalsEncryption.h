#pragma once

#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <unordered_map>
#include <unordered_set>

using namespace llvm;

namespace polaris {


struct GlobalsEncryption : PassInfoMixin<GlobalsEncryption> {
  bool EnabledFlag;
  GlobalsEncryption() : EnabledFlag(false) {}
  explicit GlobalsEncryption(bool Enabled) : EnabledFlag(Enabled) {}

  bool appleptrauth;
  bool opaquepointers;
  std::unordered_map<Function *, GlobalVariable * /*Decryption Status*/>
      encstatus;
  std::unordered_map<GlobalVariable *, std::pair<Constant *, GlobalVariable *>>
      mgv2keys;
  std::unordered_map<Constant *, SmallVector<unsigned int, 16>>
      unencryptedindex;
  SmallVector<GlobalVariable *, 32> genedgv;
  std::unordered_map<GlobalVariable *,
                     std::pair<GlobalVariable *, GlobalVariable *>>
      globalOld2New;
  std::unordered_set<GlobalVariable *> globalProcessedGVs;

  void HandleDecryptionBlock(
      BasicBlock *B, BasicBlock *C,
      std::unordered_map<GlobalVariable *,
                         std::pair<Constant *, GlobalVariable *>> &GV2Keys);

  void HandleFunction(Function *Func);
  void HandleUser(User *U, SmallVector<GlobalVariable *, 32> &Globals,
                  std::unordered_set<User *> &Users,
                  std::unordered_set<User *> &VisitedUsers);
  void
  processConstantAggregate(GlobalVariable *strGV, ConstantAggregate *CA,
                           std::unordered_set<GlobalVariable *> *rawStrings,
                           SmallVector<GlobalVariable *, 32> *unhandleablegvs,
                           SmallVector<GlobalVariable *, 32> *Globals,
                           std::unordered_set<User *> *Users, bool *breakFor);
  bool handleableGV(GlobalVariable *GV);
  GlobalVariable *ObjectiveCString(GlobalVariable *GV, std::string name,
                                   GlobalVariable *newString,
                                   ConstantStruct *CS);

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  Function *buildDecryptFunction(Module &M);
  void process(Module &M);
  static bool isRequired() { return true; }
};

}; // namespace polaris