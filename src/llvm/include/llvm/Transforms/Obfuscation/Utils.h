#include "llvm/IR/Function.h"
#include <string>
#include <vector>
using namespace llvm;
namespace polaris {
    
std::string readAnnotate(Function &f);
bool toObfuscate(bool flag, Function &f,
    std::string const &attribute);  
uint64_t getRandomNumber();
unsigned int getUniqueNumber(std::vector<unsigned int> &rand_list);
void getRandomNoRepeat(unsigned upper_bound, unsigned size,
                       std::vector<unsigned> &result);
uint64_t getInverse(uint64_t a, uint64_t m);

void demoteRegisters(Function *f);
void fixStack(Function *f);

void FixFunctionConstantExpr(Function *Func);
void turnOffOptimization(Function *f);
bool AreUsersInOneFunction(GlobalVariable *GV);
} // namespace polaris
