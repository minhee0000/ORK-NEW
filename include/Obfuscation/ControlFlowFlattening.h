#ifndef ORKNEW_CONTROL_FLOW_FLATTENING_H
#define ORKNEW_CONTROL_FLOW_FLATTENING_H

#include "llvm/IR/PassManager.h"

namespace orknew {

struct ControlFlowFlatteningPass
    : public llvm::PassInfoMixin<ControlFlowFlatteningPass> {
  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &AM);

private:
  void flatten(llvm::Function &F);
};

} // namespace orknew

#endif
