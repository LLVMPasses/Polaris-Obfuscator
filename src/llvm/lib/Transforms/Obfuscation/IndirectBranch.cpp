#include "llvm/Transforms/Obfuscation/IndirectBranch.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Obfuscation/Utils.h"

using namespace llvm;
namespace polaris {

void IndirectBranch::process(Function &F) {
  DataLayout Data = F.getParent()->getDataLayout();
  int PtrSize =
      Data.getTypeAllocSize(Type::getInt8Ty(F.getContext())->getPointerTo());
  Type *PtrValueType = Type::getIntNTy(F.getContext(), PtrSize * 8);
  std::vector<BranchInst *> Brs;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (isa<BranchInst>(I)) {
        Brs.push_back((BranchInst *)&I);
      }
    }
  }

  // 如果没有分支指令需要处理，则提前返回
  if (Brs.empty())
    return;

  std::map<BasicBlock *, IndirectBlockInfo> Map;
  std::vector<Constant *> Values;
  for (BranchInst *Br : Brs) {
    std::vector<BasicBlock *> BBs;
    if (Br->isConditional()) {
      BasicBlock *TrueBB = Br->getSuccessor(0), *FalseBB = Br->getSuccessor(1);
      BBs.push_back(TrueBB);
      BBs.push_back(FalseBB);
    } else {
      BasicBlock *BB = Br->getSuccessor(0);
      BBs.push_back(BB);
    }
    for (BasicBlock *BB : BBs) {
      if (Map.find(BB) != Map.end()) {
        continue;
      }
      IndirectBlockInfo Info;
      Info.BB = BB;
      Info.IndexWithinTable = Map.size();
      Info.RandomKey = 0;
      Map[BB] = Info;
      Values.push_back(nullptr);
    }
  }
  ArrayType *AT = ArrayType::get(
      Type::getInt8Ty(F.getContext())->getPointerTo(), Map.size());

  // 修复模板函数的问题：模板函数放在comdat节，最终只保留一实例，如果丢弃后BlockAddress::get(Info.BB)对应的函数引用丢失就没法链接
  // 修复办法：创建独立comdat
  std::string GVName = "__addr_table_" + F.getName().str();

  GlobalVariable *AddrTable = new GlobalVariable(
      *(F.getParent()), AT, false, F.getLinkage(), NULL, GVName);

  // 继承函数的 COMDAT 和可见性属性
  if (F.hasComdat()) {
    AddrTable->setComdat(F.getComdat());
  }
  AddrTable->setVisibility(F.getVisibility());
  AddrTable->setDLLStorageClass(F.getDLLStorageClass());
  // 如果函数具有内部链接，确保表也具有内部链接
  if (F.hasInternalLinkage() || F.hasPrivateLinkage()) {
    AddrTable->setLinkage(GlobalValue::PrivateLinkage);
  }


  for (auto Iter = Map.begin(); Iter != Map.end(); Iter++) {
    IndirectBlockInfo &Info = Iter->second;
    assert(Iter->first == Info.BB);
    BlockAddress *BA = BlockAddress::get(Info.BB);
    Constant *CValue = ConstantExpr::getPtrToInt(BA, PtrValueType);
    CValue = ConstantExpr::getAdd(
        CValue, ConstantInt::get(PtrValueType, Info.RandomKey));
    CValue = ConstantExpr::getIntToPtr(
        CValue, Type::getInt8Ty(F.getContext())->getPointerTo());
    Values[Info.IndexWithinTable] = CValue;
  }
  Constant *ValueArray = ConstantArray::get(AT, ArrayRef<Constant *>(Values));
  AddrTable->setInitializer(ValueArray);

  for (BranchInst *Br : Brs) {
    IRBuilder<> IRB(Br);
    if (Br->isConditional()) {
      BasicBlock *TrueBB = Br->getSuccessor(0), *FalseBB = Br->getSuccessor(1);
      IndirectBlockInfo &TI = Map[TrueBB], &FI = Map[FalseBB];
      Value *Cond = Br->getCondition();
      Value *Index = IRB.CreateSelect(Cond, IRB.getInt32(TI.IndexWithinTable),
                                      IRB.getInt32(FI.IndexWithinTable));
      Value *Item = IRB.CreateLoad(
          IRB.getInt8PtrTy(),
          IRB.CreateGEP(AT, AddrTable, {IRB.getInt32(0), Index}));

      Value *Key =
          IRB.CreateSelect(Cond, IRB.getIntN(PtrSize * 8, TI.RandomKey),
                           IRB.getIntN(PtrSize * 8, FI.RandomKey));
      Value *Addr = IRB.CreateIntToPtr(
          IRB.CreateSub(IRB.CreatePtrToInt(Item, PtrValueType), Key),
          IRB.getInt8PtrTy());

      IndirectBrInst *IBR = IRB.CreateIndirectBr(Addr);
      IBR->addDestination(TrueBB);
      IBR->addDestination(FalseBB);
      Br->eraseFromParent();
    } else {
      BasicBlock *BB = Br->getSuccessor(0);
      IndirectBlockInfo &BI = Map[BB];
      Value *Item = IRB.CreateLoad(
          IRB.getInt8PtrTy(),
          IRB.CreateGEP(AT, AddrTable,
                        {IRB.getInt32(0), IRB.getInt32(BI.IndexWithinTable)}));
      Value *Key = IRB.getIntN(PtrSize * 8, BI.RandomKey);
      Value *Addr = IRB.CreateIntToPtr(
          IRB.CreateSub(IRB.CreatePtrToInt(Item, PtrValueType), Key),
          IRB.getInt8PtrTy());
      IndirectBrInst *IBR = IRB.CreateIndirectBr(Addr);
      IBR->addDestination(BB);
      Br->eraseFromParent();
    }
  }
}
PreservedAnalyses IndirectBranch::run(Function &F,
                                      FunctionAnalysisManager &AM) {
  if (toObfuscate(EnabledFlag,F,"ibr")) {
    errs() << "try idr: "<<F.getName().str()<<"\n";
    process(F);
    return PreservedAnalyses::none();
  }

  return PreservedAnalyses::all();
}
} // namespace polaris