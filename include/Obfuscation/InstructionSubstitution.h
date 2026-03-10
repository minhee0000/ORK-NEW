#ifndef ORKNEW_INSTRUCTION_SUBSTITUTION_H
#define ORKNEW_INSTRUCTION_SUBSTITUTION_H

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"

namespace orknew {

struct InstructionSubstitutionPass
    : public llvm::PassInfoMixin<InstructionSubstitutionPass> {
  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &AM);

private:
  llvm::Value *substituteAdd(llvm::BinaryOperator *BO,
                             llvm::IRBuilder<> &Builder);
  llvm::Value *substituteAddMBA(llvm::BinaryOperator *BO,
                                llvm::IRBuilder<> &Builder);
  llvm::Value *substituteSub(llvm::BinaryOperator *BO,
                             llvm::IRBuilder<> &Builder);
  llvm::Value *substituteSubMBA(llvm::BinaryOperator *BO,
                                llvm::IRBuilder<> &Builder);
  llvm::Value *substituteXor(llvm::BinaryOperator *BO,
                             llvm::IRBuilder<> &Builder);
  llvm::Value *substituteAnd(llvm::BinaryOperator *BO,
                             llvm::IRBuilder<> &Builder);
  llvm::Value *substituteOr(llvm::BinaryOperator *BO,
                            llvm::IRBuilder<> &Builder);
};

} // namespace orknew

#endif
