#ifndef ORKNEW_SYMBOL_STRIPPING_H
#define ORKNEW_SYMBOL_STRIPPING_H

#include "llvm/IR/PassManager.h"

namespace orknew {

// 심볼 정보 제거: 함수명, 전역 변수명, RTTI 등 리버싱에 유용한 심볼 제거
struct SymbolStrippingPass : public llvm::PassInfoMixin<SymbolStrippingPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
};

} // namespace orknew

#endif
