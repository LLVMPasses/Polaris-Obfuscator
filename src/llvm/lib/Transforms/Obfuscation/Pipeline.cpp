

#include "llvm/Transforms/Obfuscation/Pipeline.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Obfuscation/AliasAccess.h"
#include "llvm/Transforms/Obfuscation/BogusControlFlow2.h"
#include "llvm/Transforms/Obfuscation/CustomCC.h"
#include "llvm/Transforms/Obfuscation/Flattening.h"
#include "llvm/Transforms/Obfuscation/GlobalsEncryption.h"
#include "llvm/Transforms/Obfuscation/IndirectBranch.h"
#include "llvm/Transforms/Obfuscation/IndirectCall.h"
#include "llvm/Transforms/Obfuscation/LinearMBA.h"
#include "llvm/Transforms/Obfuscation/MergeFunction.h"
#include "llvm/Transforms/Obfuscation/Substitution.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"
using namespace llvm;
using namespace polaris;

static cl::list<std::string> Passes("passes", cl::CommaSeparated, cl::Hidden,
                                    cl::desc("Obfuscation passes"));



//static cl::opt<bool>
//    EnableIRObfusaction("irobf", cl::init(false), cl::NotHidden,
//                        cl::desc("Enable IR Code Obfuscation."),
//                        cl::ZeroOrMore);
static cl::opt<bool>
    EnableIndirectBr("irobf-indbr", cl::init(false), cl::NotHidden,
                     cl::desc("Enable IR Indirect Branch Obfuscation."),
                     cl::ZeroOrMore);

//static cl::opt<bool>
//    EnableIndirectCall("irobf-icall", cl::init(false), cl::NotHidden,
//                       cl::desc("Enable IR Indirect Call Obfuscation."),
//                       cl::ZeroOrMore);

//static cl::opt<bool> EnableIndirectGV(
//    "irobf-indgv", cl::init(false), cl::NotHidden,
//    cl::desc("Enable IR Indirect Global Variable Obfuscation."),
//    cl::ZeroOrMore);
//
static cl::opt<bool> EnableIRFlattening(
    "irobf-cff", cl::init(false), cl::NotHidden,
    cl::desc("Enable IR Control Flow Flattening Obfuscation."), cl::ZeroOrMore);

static cl::opt<bool>
    EnableIRStringEncryption("irobf-cse", cl::init(false), cl::NotHidden,
                             cl::desc("Enable IR Constant String Encryption."), cl::ZeroOrMore);



struct LowerSwitchWrapper : LowerSwitchPass {
  static bool isRequired() { return true; }
};

ModulePassManager buildObfuscationPipeline() {
  ModulePassManager MPM;

  for (auto pass : Passes) {
    errs() << pass << "\n";
    if (EnableIRFlattening || pass == "fla") {
      errs() << "add fla pass\n";
      MPM.addPass(Flattening(EnableIRFlattening));
    } else if (EnableIRStringEncryption || pass == "gvenc") {
      MPM.addPass(GlobalsEncryption(EnableIRStringEncryption));
    } else if (EnableIndirectBr || pass == "indbr") {
      FunctionPassManager FPM;
      FPM.addPass(IndirectBranch(EnableIndirectBr));
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    } else if (pass == "indcall") {
      FunctionPassManager FPM;
      FPM.addPass(IndirectCall());
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    } else if (pass == "alias") {
      MPM.addPass(AliasAccess());
    } else if (pass == "bcf") {
      FunctionPassManager FPM;
      FPM.addPass(BogusControlFlow2());
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    } else if (pass == "ccc") {
      MPM.addPass(CustomCC());
    } else if (pass == "sub") {
      FunctionPassManager FPM;
      FPM.addPass(Substitution());
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    } else if (pass == "merge") {
      MPM.addPass(MergeFunction());
    } else if (pass == "mba") {
      FunctionPassManager FPM;
      FPM.addPass(LinearMBA());
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    }
  }
  return MPM;
}
