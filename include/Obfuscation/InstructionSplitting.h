#ifndef ORKNEW_INSTRUCTION_SPLITTING_H
#define ORKNEW_INSTRUCTION_SPLITTING_H

#include "llvm/IR/PassManager.h"

namespace orknew {

// 연속 명령어 BB 분리: 연속된 명령어를 별도 BB로 쪼개어
// CFF 적용 시 switch 복잡도를 극대화
struct InstructionSplittingPass
    : public llvm::PassInfoMixin<InstructionSplittingPass> {
  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &AM);
};

} // namespace orknew

#endif
