

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
#if LLVM_VERSION_MAJOR >= 16
static cl::list<std::string> Passes("passes", cl::CommaSeparated, cl::Hidden,
                                    cl::desc("Obfuscation passes"), cl::list_init<std::string>({"fla", "bcf", "igv", "ibr", "icall", "mba"}));
#else
static cl::list<std::string> Passes("passes", cl::CommaSeparated, cl::Hidden,
                                    cl::desc("Obfuscation passes"));
#endif

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
static cl::alias AliasEnableIRBogusControlFlow("bcf", cl::desc("Alias for irobf-bcf"),
    cl::aliasopt(EnableIRBogusControlFlow));

//控制流混淆-指令替换
static cl::opt<bool> EnableIRSubstitution("irobf-sub", cl::init(false), cl::NotHidden,
    cl::desc("Enable IR substitution."), cl::ZeroOrMore);

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
  errs() << "OLLVM-ND Passes Pipeline\n";
  ModulePassManager MPM;
  bool sobf_added = false;
  if (Passes.empty()) {
    Passes.push_back("fla");
    Passes.push_back("bcf");
    //Passes.push_back("sobf");
    Passes.push_back("igv");
    Passes.push_back("icall");
    Passes.push_back("ibr");
    Passes.push_back("mba");
  }

  for (auto pass : Passes) {
    //errs() << pass << "\n";
    if (pass == "fla") {
      errs() << "add fla pass\n";
      MPM.addPass(Flattening(EnableIRFlattening));
    } else if (pass == "sobf" && !sobf_added) { //不能重复添加这个pass
      errs() << "add sobf pass\n";
      MPM.addPass(GlobalsEncryption(EnableIRStringEncryption));
      sobf_added = true;
    } else if (pass == "ibr") {
      errs() << "add ibr pass\n";
      FunctionPassManager FPM;
      FPM.addPass(IndirectBranch(EnableIndirectBr));
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    } else if (pass == "icall") {
      errs() << "add icall pass\n";
      FunctionPassManager FPM;
      FPM.addPass(IndirectCall(EnableIndirectCall));
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    } else if (pass == "alias") {
      MPM.addPass(AliasAccess());
    } else if (pass == "bcf") {
      errs() << "add bcf pass\n";
      FunctionPassManager FPM;
      FPM.addPass(BogusControlFlow2(EnableIRBogusControlFlow));
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    } else if (pass == "ccc") {
      MPM.addPass(CustomCC());
    } else if (pass == "sub") {
      errs() << "add sub pass\n";
      FunctionPassManager FPM;
      FPM.addPass(Substitution(EnableIRSubstitution));
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    } else if (pass == "merge") {
      errs() << "add merge pass\n";
      MPM.addPass(MergeFunction(EnableIRFunctionMerge));
    } else if (pass == "mba") {
      errs() << "add mba pass\n";
      FunctionPassManager FPM;
      FPM.addPass(LinearMBA(EnableIRLinearMBA));
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    }
  }

  if (EnableIRStringEncryption&& !sobf_added) {
    errs() << "add sobf pass\n";
    MPM.addPass(GlobalsEncryption(EnableIRStringEncryption));
    sobf_added = true;
  }
  return MPM;
}