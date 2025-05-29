#include "llvm/Transforms/Obfuscation/Flattening.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Obfuscation/Utils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/ManagedStatic.h"
#include <cstdio>
#include <map>
#include <random>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>
#include <vector>

using namespace llvm;
using namespace std;
namespace polaris {

class CryptoUtils {
  public:
    CryptoUtils();
    ~CryptoUtils();
    void prng_seed(std::uint_fast64_t seed);
    void prng_seed();
    template <typename T> T get() {
      std::uint_fast64_t num = get_raw();
      return static_cast<T>(num);
    };
    // Return a value in [0,max)
    uint32_t get_range(uint32_t max) { return get_range(0, max); }
    uint32_t get_range(uint32_t min, uint32_t max);
    uint32_t get_uint32_t() { return get<uint32_t>(); };
    uint64_t get_uint64_t() { return get<uint64_t>(); };
    uint32_t get_uint8_t() { return get<uint8_t>(); };
    uint32_t get_uint16_t() { return get<uint16_t>(); };
  
    void get_bytes(char *buffer, const int len);
    unsigned scramble32(const unsigned in, const char key[16]);
    unsigned long long scramble64(const unsigned in, const char key[16]);
    // Scramble32 originally uses AES to generates the mapping relationship
    // between a BB and its switchvar Hikari updates this by doing this using
    // mt19937_64 in C++ STLs which is a faster but less cryprographically secured
    // This method try to find the corresponding value from the VMap first, if not
    // then use RNG to generate,fill and return the value
    uint32_t
    scramble32(uint32_t in,
               std::unordered_map<uint32_t /*IDX*/, uint32_t /*VAL*/> &VMap);
  
  private:
    std::mt19937_64 *eng = nullptr;
    std::uint_fast64_t get_raw();
};


uint32_t CryptoUtils::scramble32(
  uint32_t in, std::unordered_map<uint32_t /*IDX*/, uint32_t /*VAL*/> &VMap) {
if (VMap.find(in) == VMap.end()) {
  uint32_t V = get_uint32_t();
  VMap[in] = V;
  return V;
} else {
  return VMap[in];
}
}
CryptoUtils::~CryptoUtils() {
if (eng != nullptr)
  delete eng;
}
void CryptoUtils::prng_seed() {
using namespace std::chrono;
std::uint_fast64_t ms =
    duration_cast<milliseconds>(system_clock::now().time_since_epoch())
        .count();
errs() << format("std::mt19937_64 seeded with current timestamp: %" PRIu64 "",
                 ms)
       << "\n";
eng = new std::mt19937_64(ms);
}
void CryptoUtils::prng_seed(std::uint_fast64_t seed) {
errs() << format("std::mt19937_64 seeded with: %" PRIu64 "", seed) << "\n";
eng = new std::mt19937_64(seed);
}
std::uint_fast64_t CryptoUtils::get_raw() {
if (eng == nullptr)
  prng_seed();
return (*eng)();
}
uint32_t CryptoUtils::get_range(uint32_t min, uint32_t max) {
if (max == 0)
  return 0;
std::uniform_int_distribution<uint32_t> dis(min, max - 1);
return dis(*eng);
}

void CryptoUtils::get_bytes(char *buffer, const int len) {
if (eng == nullptr)
  prng_seed();

for (int i = 0; i < len; i++) {
  buffer[i] = static_cast<char>(get_raw() & 0xFF);
}
}

unsigned CryptoUtils::scramble32(const unsigned in, const char key[16]) {
if (eng == nullptr)
  prng_seed();

// Use the input and key to seed a temporary RNG
std::uint_fast64_t seed = in;
for (int i = 0; i < 16; i++) {
  seed = seed * 31 + static_cast<unsigned char>(key[i]);
}

std::mt19937_64 temp_eng(seed);
return static_cast<unsigned>(temp_eng());
}

unsigned long long CryptoUtils::scramble64(const unsigned in, const char key[16]) {
if (eng == nullptr)
  prng_seed();

// Use the input and key to seed a temporary RNG
std::uint_fast64_t seed = in;
for (int i = 0; i < 16; i++) {
  seed = seed * 31 + static_cast<unsigned char>(key[i]);
}

std::mt19937_64 temp_eng(seed);
return temp_eng();
}
CryptoUtils::CryptoUtils() {}
ManagedStatic<CryptoUtils> cryptoutils;

//全局加密状态
extern ManagedStatic<CryptoUtils> cryptoutils;

  
Function *Flattening::buildUpdateKeyFunc(Module *m) {
  std::vector<Type *> params;
  params.push_back(Type::getInt8Ty(m->getContext()));
  params.push_back(Type::getInt32Ty(m->getContext()));
  params.push_back(Type::getInt32Ty(m->getContext())->getPointerTo());
  params.push_back(Type::getInt32Ty(m->getContext())->getPointerTo());
  params.push_back(Type::getInt32Ty(m->getContext()));
  FunctionType *funcType =
      FunctionType::get(Type::getVoidTy(m->getContext()), params, false);
  Function *func = Function::Create(funcType, GlobalValue::PrivateLinkage,
                                    Twine("ollvm.obf.flatten.buildUpdateKeyFunc"), m);
  BasicBlock *entry = BasicBlock::Create(m->getContext(), "entry", func);
  BasicBlock *cond = BasicBlock::Create(m->getContext(), "cond", func);
  BasicBlock *update = BasicBlock::Create(m->getContext(), "update", func);
  BasicBlock *end = BasicBlock::Create(m->getContext(), "end", func);
  Function::arg_iterator iter = func->arg_begin();
  Value *flag = iter;
  Value *len = ++iter;
  Value *posArray = ++iter;
  Value *keyArray = ++iter;
  Value *num = ++iter;
  IRBuilder<> irb(entry);
  Value *i = irb.CreateAlloca(irb.getInt32Ty());
  irb.CreateStore(irb.getInt32(0), i);
  irb.CreateCondBr(irb.CreateICmpEQ(flag, irb.getInt8(0)), cond, end);

  irb.SetInsertPoint(cond);
  irb.CreateCondBr(irb.CreateICmpSLT(irb.CreateLoad(irb.getInt32Ty(), i), len),
                   update, end);

  irb.SetInsertPoint(update);

  Value *pos = irb.CreateLoad(
      irb.getInt32Ty(), irb.CreateGEP(irb.getInt32Ty(), posArray,
                                      irb.CreateLoad(irb.getInt32Ty(), i)));
  Value *key = irb.CreateGEP(irb.getInt32Ty(), keyArray, pos);
  irb.CreateStore(irb.CreateXor(irb.CreateLoad(irb.getInt32Ty(), key), num),
                  key);
  irb.CreateStore(
      irb.CreateAdd(irb.CreateLoad(irb.getInt32Ty(), i), irb.getInt32(1)), i);
  irb.CreateBr(cond);

  irb.SetInsertPoint(end);
  irb.CreateRetVoid();
  return func;
}

bool flatten(Function* f,unsigned pointerSize) {
  vector<BasicBlock*> origBB;
  BasicBlock* loopEntry;
  BasicBlock* loopEnd;
  LoadInst* load;
  SwitchInst* switchI;
  AllocaInst* switchVar;

  // SCRAMBLER
  char scrambling_key[16];
  cryptoutils->get_bytes(scrambling_key, 16);
  // END OF SCRAMBLER

  // Lower switch
  PassBuilder PB;
  FunctionAnalysisManager FAM;
  FunctionPassManager FPM;
  PB.registerFunctionAnalyses(FAM);
  FPM.addPass(LowerSwitchPass());
  FPM.run(*f, FAM);

  // Save all original BB
  for (Function::iterator i = f->begin(); i != f->end(); ++i) {
      BasicBlock* tmp = &*i;
      origBB.push_back(tmp);

      BasicBlock* bb = &*i;
      if (isa<InvokeInst>(bb->getTerminator())) {
          return false;
      }
  }

  // Nothing to flatten
  if (origBB.size() <= 1) {
      return false;
  }

  LLVMContext& Ctx = f->getContext();
  IntegerType* intType = Type::getInt32Ty(Ctx);
  if (pointerSize == 8) {
      intType = Type::getInt64Ty(Ctx);
  }

  Value* MySecret = ConstantInt::get(intType, 0, true);

  // Remove first BB
  origBB.erase(origBB.begin());

  // Get a pointer on the first BB
  Function::iterator tmp = f->begin();  //++tmp;
  BasicBlock* insert = &*tmp;

  // If main begin with an if
  BranchInst* br = NULL;
  if (isa<BranchInst>(insert->getTerminator())) {
      br = cast<BranchInst>(insert->getTerminator());
  }

  if ((br != NULL && br->isConditional()) ||
      insert->getTerminator()->getNumSuccessors() > 1) {
      BasicBlock::iterator i = insert->end();
      --i;

      if (insert->size() > 1) {
          --i;
      }

      BasicBlock* tmpBB = insert->splitBasicBlock(i, "first");
      origBB.insert(origBB.begin(), tmpBB);
  }

  // Remove jump
  insert->getTerminator()->eraseFromParent();

  // Create switch variable and set as it
  switchVar =
      new AllocaInst(intType, 0, "switchVar", insert);
  if (pointerSize == 8) {
      new StoreInst(
          ConstantInt::get(intType,
              cryptoutils->scramble64(0, scrambling_key)),
          switchVar, insert);
  }
  else {
      new StoreInst(
          ConstantInt::get(intType,
              cryptoutils->scramble32(0, scrambling_key)),
          switchVar, insert);
  }

  // Create main loop
  loopEntry = BasicBlock::Create(f->getContext(), "loopEntry", f, insert);
  loopEnd = BasicBlock::Create(f->getContext(), "loopEnd", f, insert);

  load = new LoadInst(intType, switchVar, "switchVar", loopEntry);

  // Move first BB on top
  insert->moveBefore(loopEntry);
  BranchInst::Create(loopEntry, insert);

  // loopEnd jump to loopEntry
  BranchInst::Create(loopEntry, loopEnd);

  BasicBlock* swDefault =
      BasicBlock::Create(f->getContext(), "switchDefault", f, loopEnd);
  BranchInst::Create(loopEnd, swDefault);

  // Create switch instruction itself and set condition
  switchI = SwitchInst::Create(&*f->begin(), swDefault, 0, loopEntry);
  switchI->setCondition(load);

  // Remove branch jump from 1st BB and make a jump to the while
  f->begin()->getTerminator()->eraseFromParent();

  BranchInst::Create(loopEntry, &*f->begin());

  // Put all BB in the switch
  for (vector<BasicBlock*>::iterator b = origBB.begin(); b != origBB.end();
      ++b) {
      BasicBlock* i = *b;
      ConstantInt* numCase = NULL;

      // Move the BB inside the switch (only visual, no code logic)
      i->moveBefore(loopEnd);

      // Add case to switch
      if (pointerSize == 8) {
          numCase = cast<ConstantInt>(ConstantInt::get(
              switchI->getCondition()->getType(),
              cryptoutils->scramble64(switchI->getNumCases(), scrambling_key)));
      }
      else {
          numCase = cast<ConstantInt>(ConstantInt::get(
              switchI->getCondition()->getType(),
              cryptoutils->scramble32(switchI->getNumCases(), scrambling_key)));
      }
      switchI->addCase(numCase, i);
  }

  ConstantInt* Zero = ConstantInt::get(intType, 0);
  // Recalculate switchVar
  for (vector<BasicBlock*>::iterator b = origBB.begin(); b != origBB.end();
      ++b) {
      BasicBlock* i = *b;
      ConstantInt* numCase = NULL;

      // Ret BB
      if (i->getTerminator()->getNumSuccessors() == 0) {
          continue;
      }

      // If it's a non-conditional jump
      if (i->getTerminator()->getNumSuccessors() == 1) {
          // Get successor and delete terminator
          BasicBlock* succ = i->getTerminator()->getSuccessor(0);
          i->getTerminator()->eraseFromParent();

          // Get next case
          numCase = switchI->findCaseDest(succ);

          // If next case == default case (switchDefault)
          if (numCase == NULL) {
              if (pointerSize == 8) {
                  numCase = cast<ConstantInt>(
                      ConstantInt::get(switchI->getCondition()->getType(),
                          cryptoutils->scramble64(
                              switchI->getNumCases() - 1, scrambling_key)));
              }
              else {
                  numCase = cast<ConstantInt>(
                      ConstantInt::get(switchI->getCondition()->getType(),
                          cryptoutils->scramble32(
                              switchI->getNumCases() - 1, scrambling_key)));
              }
          }

          // numCase = MySecret - (MySecret - numCase)
          // X = MySecret - numCase
          Constant* X = ConstantExpr::getSub(Zero, numCase);
          Value* newNumCase = BinaryOperator::Create(Instruction::Sub, MySecret, X, "", i);

          // Update switchVar and jump to the end of loop
          new StoreInst(newNumCase, load->getPointerOperand(), i);
          BranchInst::Create(loopEnd, i);
          continue;
      }

      // If it's a conditional jump
      if (i->getTerminator()->getNumSuccessors() == 2) {
          // Get next cases
          ConstantInt* numCaseTrue =
              switchI->findCaseDest(i->getTerminator()->getSuccessor(0));
          ConstantInt* numCaseFalse =
              switchI->findCaseDest(i->getTerminator()->getSuccessor(1));

          // Check if next case == default case (switchDefault)
          if (numCaseTrue == NULL) {
              if (pointerSize == 8) {
                  numCaseTrue = cast<ConstantInt>(
                      ConstantInt::get(switchI->getCondition()->getType(),
                          cryptoutils->scramble64(
                              switchI->getNumCases() - 1, scrambling_key)));
              }
              else {
                  numCaseTrue = cast<ConstantInt>(
                      ConstantInt::get(switchI->getCondition()->getType(),
                          cryptoutils->scramble32(
                              switchI->getNumCases() - 1, scrambling_key)));
              }
          }

          if (numCaseFalse == NULL) {
              if (pointerSize == 8) {
                  numCaseFalse = cast<ConstantInt>(
                      ConstantInt::get(switchI->getCondition()->getType(),
                          cryptoutils->scramble64(
                              switchI->getNumCases() - 1, scrambling_key)));
              }
              else {
                  numCaseFalse = cast<ConstantInt>(
                      ConstantInt::get(switchI->getCondition()->getType(),
                          cryptoutils->scramble32(
                              switchI->getNumCases() - 1, scrambling_key)));
              }
          }

          Constant* X, * Y;
          X = ConstantExpr::getSub(Zero, numCaseTrue);
          Y = ConstantExpr::getSub(Zero, numCaseFalse);
          Value* newNumCaseTrue = BinaryOperator::Create(Instruction::Sub, MySecret, X, "", i->getTerminator());
          Value* newNumCaseFalse = BinaryOperator::Create(Instruction::Sub, MySecret, Y, "", i->getTerminator());

          // Create a SelectInst
          BranchInst* br = cast<BranchInst>(i->getTerminator());
          SelectInst* sel =
              SelectInst::Create(br->getCondition(), newNumCaseTrue, newNumCaseFalse, "",
                  i->getTerminator());

          // Erase terminator
          i->getTerminator()->eraseFromParent();

          // Update switchVar and jump to the end of loop
          new StoreInst(sel, load->getPointerOperand(), i);
          BranchInst::Create(loopEnd, i);
          continue;
      }
  }

  fixStack(f);

  FPM.run(*f, FAM);

  return true;
}


void Flattening::doFlatten(Function *f, int seed, Function *updateFunc, unsigned pointerSize) {
  //srand(seed);
  flatten(f,pointerSize);
}

PreservedAnalyses Flattening::run(Module &M, ModuleAnalysisManager &AM) {
  Function *updateFunc = buildUpdateKeyFunc(&M);
  unsigned pointerSize = M.getDataLayout().getTypeAllocSize(PointerType::getUnqual(M.getContext()));
  for (Function &F : M) {
    if (&F == updateFunc)
      continue;
    if ( toObfuscate(EnabledFlag,F,"fla")) {
      
      errs() << "try flattern: "<<F.getName().str()<<"\n";
      doFlatten(&F, 0, updateFunc, pointerSize);
    }
  }

  return PreservedAnalyses::none();
}
} // namespace polaris
