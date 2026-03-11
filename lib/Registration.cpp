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
                  // ConstOb/InstrSub → CFF → BB 분리
                  // BB 분리는 CFF 이후 실행: CFF 케이스 BB를 추가 분할하여
                  // 분석기의 패턴 인식을 방해하면서 CFF 안정성 보장

                  // 1. 재배치 (함수/BB 순서 셔플)
                  MPM.addPass(orknew::RelocationPass());

                  // 2. 문자열 난독화 (힙 기반 지연 복호화 + anti-prediction)
                  MPM.addPass(orknew::StringEncryptionPass());

                  // 3. 상수 난독화
                  MPM.addPass(createModuleToFunctionPassAdaptor(
                      orknew::ConstantObfuscationPass()));

                  // 4. 이진 연산 치환 (MBA 패턴 포함)
                  MPM.addPass(createModuleToFunctionPassAdaptor(
                      orknew::InstructionSubstitutionPass()));

                  // 5. 제어 흐름 평탄화 (switch + opaque predicate + bogus BB)
                  MPM.addPass(createModuleToFunctionPassAdaptor(
                      orknew::ControlFlowFlatteningPass()));

                  // 6. BB 분리 (CFF 이후 — CFF 케이스 BB를 추가 분할)
                  MPM.addPass(createModuleToFunctionPassAdaptor(
                      orknew::InstructionSplittingPass()));

                  // 7. 심볼 정보 제거 (마지막에 실행)
                  MPM.addPass(orknew::SymbolStrippingPass());
                });
          }};
}
