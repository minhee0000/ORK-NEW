#ifndef ORKNEW_CONSTANT_OBFUSCATION_H
#define ORKNEW_CONSTANT_OBFUSCATION_H

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"

namespace orknew {

// 상수 접근 연산화: 즉시값(0x73 등)을 복잡한 연산식으로 치환
struct ConstantObfuscationPass
    : public llvm::PassInfoMixin<ConstantObfuscationPass> {
  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &AM);
};

} // namespace orknew

#endif
