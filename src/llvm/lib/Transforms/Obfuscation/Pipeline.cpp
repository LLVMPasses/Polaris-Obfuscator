

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

//通过opt数组控制可以插入pass顺序 提供默认顺序
static cl::list<std::string> Passes("passes", cl::CommaSeparated, cl::Hidden,
                                    cl::desc("Obfuscation passes"), cl::list_init<std::string>({"fla", "bcf", "sobf", "igv", "ibr", "icall", "mba"}));



//static cl::opt<bool>
//    EnableIRObfusaction("irobf", cl::init(false), cl::NotHidden,
//                        cl::desc("Enable IR Code Obfuscation."),
//                        cl::ZeroOrMore);

//ARKARI兼容开关
//控制流混淆-跳转间接化
static cl::opt<bool> EnableIndirectBr("irobf-indbr", cl::init(false), cl::NotHidden,
     cl::desc("Enable IR Indirect Branch Obfuscation."), cl::ZeroOrMore);
static cl::alias AliasEnableIndirectBr("ibr", cl::desc("Alias for irobf-indbr"),
     cl::aliasopt(EnableIndirectBr));

//控制流混淆-调用间接化
static cl::opt<bool> EnableIndirectCall("irobf-icall", cl::init(false), cl::NotHidden,
    cl::desc("Enable IR Indirect Call Obfuscation."), cl::ZeroOrMore);
static cl::alias AliasEnableIndirectCall("icall", cl::desc("Alias for irobf-icall"),
    cl::aliasopt(EnableIndirectCall));

////控制流混淆-全局变量访问间接化
//static cl::opt<bool> EnableIndirectGV("irobf-indgv", cl::init(false), cl::NotHidden,
//    cl::desc("Enable IR Indirect Global Variable Obfuscation."),cl::ZeroOrMore);
//static cl::alias PassesAlias("igv", cl::desc("Alias for irobf-indgv"),
//    cl::aliasopt(EnableIndirectGV));

//控制流混淆-控制流平坦化
static cl::opt<bool> EnableIRFlattening("irobf-cff", cl::init(false), cl::NotHidden,
    cl::desc("Enable IR Control Flow Flattening Obfuscation."), cl::ZeroOrMore);
static cl::alias AliasEnableIRFlattening("fla", cl::desc("Alias for irobf-cff"),
    cl::aliasopt(EnableIRFlattening));

//控制流混淆-全局字符串加密
static cl::opt<bool> EnableIRStringEncryption("irobf-cse", cl::init(false), cl::NotHidden,
    cl::desc("Enable IR Constant String Encryption."), cl::ZeroOrMore);
static cl::alias AliasEnableIRStringEncryption("sobf", cl::desc("Alias for irobf-cse"),
    cl::aliasopt(EnableIRStringEncryption));

//新增
//控制流混淆-虚假控制流
static cl::opt<bool> EnableIRBogusControlFlow("irobf-bcf", cl::init(false), cl::NotHidden,
    cl::desc("Enable IR BogusControlFlow."), cl::ZeroOrMore);
static cl::alias AliasEnableIRBogusControlFlow("sobf", cl::desc("Alias for irobf-bcf"),
    cl::aliasopt(EnableIRBogusControlFlow));

//控制流混淆-线性MBA替换
static cl::opt<bool> EnableIRLinearMBA("irobf-mba", cl::init(false), cl::NotHidden,
    cl::desc("Enable IR Linear MBA substitution."), cl::ZeroOrMore);
//控制流混淆-函数合并
static cl::opt<bool> EnableIRFunctionMerge("irobf-merge", cl::init(false), cl::NotHidden,
cl::desc("Enable IR Linear MBA substitution."), cl::ZeroOrMore);



struct LowerSwitchWrapper : LowerSwitchPass {
  static bool isRequired() { return true; }
};

ModulePassManager buildObfuscationPipeline() {
  ModulePassManager MPM;

  for (auto pass : Passes) {
    //errs() << pass << "\n";
    if (EnableIRFlattening || pass == "fla") {
      errs() << "add fla pass\n";
      MPM.addPass(Flattening(EnableIRFlattening));
    } else if (EnableIRStringEncryption || pass == "sobf") {
      errs() << "add sobf pass\n";
      MPM.addPass(GlobalsEncryption(EnableIRStringEncryption));
    } else if (EnableIndirectBr || pass == "ibr") {
      errs() << "add ibr pass\n";
      FunctionPassManager FPM;
      FPM.addPass(IndirectBranch(EnableIndirectBr));
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    } else if (pass == "icall") {
      FunctionPassManager FPM;
      FPM.addPass(IndirectCall(EnableIndirectCall));
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    } else if (pass == "alias") {
      MPM.addPass(AliasAccess());
    } else if (pass == "bcf") {
      FunctionPassManager FPM;
      FPM.addPass(BogusControlFlow2(EnableIRBogusControlFlow));
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    } else if (pass == "ccc") {
      MPM.addPass(CustomCC());
    } else if (pass == "sub") {
      FunctionPassManager FPM;
      FPM.addPass(Substitution());
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    } else if (pass == "merge") {
      MPM.addPass(MergeFunction(EnableIRFunctionMerge));
    } else if (pass == "mba") {
      FunctionPassManager FPM;
      FPM.addPass(LinearMBA(EnableIRLinearMBA));
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    }
  }
  return MPM;
}
