#include "Obfuscation/SymbolStripping.h"
#include "Obfuscation/Config.h"
#include "llvm/IR/Module.h"
#include <random>

using namespace llvm;

namespace orknew {

// RTTI 관련 전역 변수 패턴
static bool isRTTISymbol(StringRef Name) {
  return Name.starts_with("_ZTI") || // typeinfo
         Name.starts_with("_ZTS") || // typeinfo name
         Name.starts_with("_ZTV") || // vtable
         Name.starts_with("_ZTC") || // construction vtable
         Name.starts_with("GCC_except_table");
}

PreservedAnalyses SymbolStrippingPass::run(Module &M,
                                           ModuleAnalysisManager &AM) {
  bool Changed = false;
  std::mt19937 Rng(std::random_device{}());

  // 내부 함수명을 의미 없는 이름으로 변경
  unsigned FuncIdx = 0;
  for (auto &F : M) {
    if (F.isDeclaration())
      continue;
    // 외부 링크 함수(main, 라이브러리 API)는 유지
    if (F.hasExternalLinkage() && (F.getName() == "main"))
      continue;
    // 런타임 함수는 유지
    if (F.getName().starts_with("__kld_") || F.getName().starts_with("kld_"))
      continue;

    if (!Config::getInstance().shouldObfuscate(F.getName()))
      continue;

    // internal/private 링크 함수만 이름 변경
    if (F.hasInternalLinkage() || F.hasPrivateLinkage()) {
      F.setName("kld.f" + std::to_string(Rng() % 999999));
      Changed = true;
    }
    FuncIdx++;
  }

  // RTTI 심볼 및 불필요한 전역 변수명 제거
  for (auto &GV : M.globals()) {
    if (GV.isDeclaration())
      continue;

    // RTTI 심볼: internal로 변경 + 이름 난독화
    if (isRTTISymbol(GV.getName())) {
      if (!GV.hasExternalLinkage() || GV.use_empty()) {
        GV.setName("kld.g" + std::to_string(Rng() % 999999));
        Changed = true;
      }
    }

    // 내부 전역 변수명 난독화
    if (GV.hasInternalLinkage() || GV.hasPrivateLinkage()) {
      GV.setName("kld.d" + std::to_string(Rng() % 999999));
      Changed = true;
    }
  }

  // BasicBlock 이름 제거 (디버그 정보 노출 방지)
  for (auto &F : M) {
    for (auto &BB : F) {
      if (BB.hasName() && !BB.getName().starts_with("kld."))
        BB.setName("");
    }
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace orknew
