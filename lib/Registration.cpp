#include "Obfuscation/Config.h"
#include "Obfuscation/ConstantObfuscation.h"
#include "Obfuscation/ControlFlowFlattening.h"
#include "Obfuscation/InstructionSplitting.h"
#include "Obfuscation/InstructionSubstitution.h"
#include "Obfuscation/Relocation.h"
#include "Obfuscation/StringEncryption.h"
#include "Obfuscation/SymbolStripping.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

// 설정 파일 경로 (커맨드라인 옵션)
static cl::opt<std::string>
    ConfigFile("kld-config", cl::desc("ORK-NEW config file path"),
               cl::value_desc("filename"), cl::init(""));

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "ORK-NEW", "0.2.0",
          [](PassBuilder &PB) {
            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel,
                   ThinOrFullLTOPhase) {
                  // 설정 파일 로드
                  if (!ConfigFile.empty())
                    orknew::Config::getInstance().loadFromFile(
                        ConfigFile);

                  // ORK 난독화 파이프라인 (순서 중요)
                  // 1) BB 분리 먼저 → 2) ConstOb/InstrSub → 3) CFF
                  // 이 순서로 BB 분리 후 추가된 명령어가 같은 BB에 유지되어
                  // CFF의 도미넌스 파괴로 인한 크래시 방지

                  // 1. 재배치 (함수/BB 순서 셔플)
                  MPM.addPass(orknew::RelocationPass());

                  // 2. 문자열 난독화 (힙 기반 지연 복호화 + anti-prediction)
                  MPM.addPass(orknew::StringEncryptionPass());

                  // 3. BB 분리 (CFF 전에 실행 — 분리 후 CFF 복잡도 증가)
                  MPM.addPass(createModuleToFunctionPassAdaptor(
                      orknew::InstructionSplittingPass()));

                  // 4. 상수 난독화 (CFF 이전, BB 분리 이후)
                  MPM.addPass(createModuleToFunctionPassAdaptor(
                      orknew::ConstantObfuscationPass()));

                  // 5. 이진 연산 치환 (MBA 패턴 포함)
                  MPM.addPass(createModuleToFunctionPassAdaptor(
                      orknew::InstructionSubstitutionPass()));

                  // 6. 제어 흐름 평탄화 (switch + opaque predicate + bogus BB)
                  MPM.addPass(createModuleToFunctionPassAdaptor(
                      orknew::ControlFlowFlatteningPass()));

                  // 7. 심볼 정보 제거 (마지막에 실행)
                  MPM.addPass(orknew::SymbolStrippingPass());
                });
          }};
}
