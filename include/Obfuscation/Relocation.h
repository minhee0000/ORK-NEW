#ifndef ORKNEW_RELOCATION_H
#define ORKNEW_RELOCATION_H

#include "llvm/IR/PassManager.h"

namespace orknew {

// ORK 재배치 난독화: 함수/전역 변수 순서를 랜덤으로 셔플하여
// 동일 소스 코드에서도 매번 다른 바이너리 레이아웃 생성
struct RelocationPass : public llvm::PassInfoMixin<RelocationPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
};

} // namespace orknew

#endif
