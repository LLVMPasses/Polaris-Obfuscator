#include "llvm/Transforms/Obfuscation/Utils.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Transforms/Utils/Local.h"
#include <algorithm>
#include <ctime>
#include <random>

#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/NoFolder.h"
#include "llvm/Support/raw_ostream.h"
#include <set>
#include <sstream>


using namespace llvm;
namespace polaris {

std::string readAnnotate(Function &f) {
  std::string annotation = "";
  GlobalVariable *glob =
      f.getParent()->getGlobalVariable("llvm.global.annotations");
  if (glob != NULL) {
    if (ConstantArray *ca = dyn_cast<ConstantArray>(glob->getInitializer())) {
      for (unsigned i = 0; i < ca->getNumOperands(); ++i) {
        if (ConstantStruct *structAn =
                dyn_cast<ConstantStruct>(ca->getOperand(i))) {
          if (structAn->getOperand(0) == &f) {
            if (GlobalVariable *annoteStr =
                    dyn_cast<GlobalVariable>(structAn->getOperand(1))) {
              if (ConstantDataSequential *data =
                      dyn_cast<ConstantDataSequential>(
                          annoteStr->getInitializer())) {
                if (data->isString()) {
                  annotation += data->getAsString().lower() + " ";
                }
              }
            }
          }
          // structAn->dump();
        }
      }
    }
  }
  return (annotation);
}

bool toObfuscate(bool flag, Function &f,
  std::string const &attribute){

    std::string attr = attribute;
    std::string attrNo = "no" + attr;
    // Check if declaration
    if (f.isDeclaration()) {
      return false;
    }
    // 仅定义
    if (f.hasAvailableExternallyLinkage() != 0) {
      return false;
    }

    if (readAnnotate(f).find(attrNo) != std::string::npos){
      return false;
    }

    if (readAnnotate(f).find(attr) != std::string::npos){
      return true;
    }

    //过滤混淆自身产生的什么函数
    if (f.getName().startswith("ollvm")){
      return false;
    }
    //混淆内部函数会崩 未知原因 先这样处理吧
    if (f.getName().startswith("llvm.")){
      return false;
    }
    //同上
    if (f.getLinkage() == llvm::GlobalValue::InternalLinkage) { 
      return false;
    }

    //返回默认值
    return flag;
}

uint64_t getRandomNumber() {
  return (((uint64_t)rand()) << 32) | ((uint64_t)rand());
}
uint32_t getUniqueNumber(std::vector<uint32_t> &rand_list) {
  uint32_t num = getRandomNumber() & 0xffffffff;
  while (true) {
    bool state = true;
    for (auto n = rand_list.begin(); n != rand_list.end(); n++) {
      if (*n == num) {
        state = false;
        break;
      }
    }

    if (state)
      break;
    num = getRandomNumber() & 0xffffffff;
  }
  return num;
}

void getRandomNoRepeat(unsigned upper_bound, unsigned size,
                       std::vector<unsigned> &result) {
  assert(upper_bound >= size);
  std::vector<unsigned> list;
  for (unsigned i = 0; i < upper_bound; i++) {
    list.push_back(i);
  }

  std::shuffle(list.begin(), list.end(), std::default_random_engine());
  for (unsigned i = 0; i < size; i++) {
    result.push_back(list[i]);
  }
}
// ax = 1 (mod m)
void exgcd(uint64_t a, uint64_t b, uint64_t &d, uint64_t &x, uint64_t &y) {
  if (!b) {
    d = a, x = 1, y = 0;
  } else {
    exgcd(b, a % b, d, y, x);
    y -= x * (a / b);
  }
}
uint64_t getInverse(uint64_t a, uint64_t m) {
  assert(a != 0);
  uint64_t x, y, d;
  exgcd(a, m, d, x, y);
  return d == 1 ? (x + m) % m : 0;
}
void demoteRegisters(Function *f) {
  std::vector<PHINode *> tmpPhi;
  std::vector<Instruction *> tmpReg;
  BasicBlock *bbEntry = &*f->begin();
  for (Function::iterator i = f->begin(); i != f->end(); i++) {
    for (BasicBlock::iterator j = i->begin(); j != i->end(); j++) {
      if (isa<PHINode>(j)) {
        PHINode *phi = cast<PHINode>(j);
        tmpPhi.push_back(phi);
        continue;
      }
      if (!(isa<AllocaInst>(j) && j->getParent() == bbEntry) &&
          j->isUsedOutsideOfBlock(&*i)) {
        tmpReg.push_back(&*j);
        continue;
      }
    }
  }
  for (unsigned int i = 0; i < tmpReg.size(); i++)
    DemoteRegToStack(*tmpReg.at(i), f->begin()->getTerminator());
  for (unsigned int i = 0; i < tmpPhi.size(); i++)
    DemotePHIToStack(tmpPhi.at(i), f->begin()->getTerminator());
}


// Shamefully borrowed from ../Scalar/RegToMem.cpp :(
  bool valueEscapes(Instruction *Inst) {
    BasicBlock *BB = Inst->getParent();
    for (Value::use_iterator UI = Inst->use_begin(), E = Inst->use_end(); UI != E;
         ++UI) {
      Instruction *I = cast<Instruction>(*UI);
      if (I->getParent() != BB || isa<PHINode>(I)) {
        return true;
      }
    }
    return false;
  }
  void fixStack(Function *f) {
    // Try to remove phi node and demote reg to stack
    SmallVector<PHINode *, 8> tmpPhi;
    SmallVector<Instruction *, 32> tmpReg;
    BasicBlock *bbEntry = &*f->begin();
    // Find first non-alloca instruction and create insertion point. This is
    // safe if block is well-formed: it always have terminator, otherwise
    // we'll get and assertion.
    BasicBlock::iterator I = bbEntry->begin();
    while (isa<AllocaInst>(I))
      ++I;
    Instruction *AllocaInsertionPoint = &*I;
    do {
      tmpPhi.clear();
      tmpReg.clear();
      for (BasicBlock &i : *f) {
        for (Instruction &j : i) {
          if (isa<PHINode>(&j)) {
            PHINode *phi = cast<PHINode>(&j);
            tmpPhi.emplace_back(phi);
            continue;
          }
          if (!(isa<AllocaInst>(&j) && j.getParent() == bbEntry) &&
              (valueEscapes(&j) || j.isUsedOutsideOfBlock(&i))) {
            tmpReg.emplace_back(&j);
            continue;
          }
        }
      }
  #if LLVM_VERSION_MAJOR >= 19
      for (Instruction *I : tmpReg)
        DemoteRegToStack(*I, false, AllocaInsertionPoint->getIterator());
      for (PHINode *P : tmpPhi)
        DemotePHIToStack(P, AllocaInsertionPoint->getIterator());
  #else
      for (Instruction *I : tmpReg)
        DemoteRegToStack(*I, false, AllocaInsertionPoint);
      for (PHINode *P : tmpPhi)
        DemotePHIToStack(P, AllocaInsertionPoint);
  #endif
    } while (tmpReg.size() != 0 || tmpPhi.size() != 0);
  }


  void FixBasicBlockConstantExpr(BasicBlock *BB) {
    // Replace ConstantExpr with equal instructions
    // Otherwise replacing on Constant will crash the compiler
    // Things to note:
    // - Phis must be placed at BB start so CEs must be placed prior to current
    // BB
    assert(!BB->empty() && "BasicBlock is empty!");
    assert(BB->getParent() && "BasicBlock must be in a Function!");
    Instruction *FunctionInsertPt =
        &*(BB->getParent()->getEntryBlock().getFirstInsertionPt());

    for (Instruction &I : *BB) {
      if (isa<LandingPadInst>(I) || isa<FuncletPadInst>(I) ||
          isa<IntrinsicInst>(I))
        continue;
      for (unsigned int i = 0; i < I.getNumOperands(); i++)
        if (ConstantExpr *C = dyn_cast<ConstantExpr>(I.getOperand(i))) {
          IRBuilder<NoFolder> IRB(&I);
          if (isa<PHINode>(I))
            IRB.SetInsertPoint(FunctionInsertPt);
          Instruction *Inst = IRB.Insert(C->getAsInstruction());
          I.setOperand(i, Inst);
        }
    }
  }

  void FixFunctionConstantExpr(Function *Func) {
    // Replace ConstantExpr with equal instructions
    // Otherwise replacing on Constant will crash the compiler
    for (BasicBlock &BB : *Func)
      FixBasicBlockConstantExpr(&BB);
  }

  void turnOffOptimization(Function *f) {
    f->removeFnAttr(Attribute::AttrKind::MinSize);
    f->removeFnAttr(Attribute::AttrKind::OptimizeForSize);
    if (!f->hasFnAttribute(Attribute::AttrKind::OptimizeNone) &&
        !f->hasFnAttribute(Attribute::AttrKind::AlwaysInline)) {
      f->addFnAttr(Attribute::AttrKind::OptimizeNone);
      f->addFnAttr(Attribute::AttrKind::NoInline);
    }
  }

  static inline std::vector<std::string> splitString(std::string str) {
    std::stringstream ss(str);
    std::string word;
    std::vector<std::string> words;
    while (ss >> word)
      words.emplace_back(word);
    return words;
  }

  bool AreUsersInOneFunction(GlobalVariable *GV) {
    SmallPtrSet<Function *, 6> userFunctions;
    for (User *U : GV->users()) {
      if (Instruction *I = dyn_cast<Instruction>(U)) {
        userFunctions.insert(I->getFunction());
      } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(U)) {
        for (User *U2 : CE->users()) {
          if (Instruction *I = dyn_cast<Instruction>(U2)) {
            userFunctions.insert(I->getFunction());
          }
        }
      } else {
        return false;
      }
    }
    return userFunctions.size() <= 1;
  }

} // namespace polaris



