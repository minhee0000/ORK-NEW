#ifndef ORKNEW_STRING_ENCRYPTION_H
#define ORKNEW_STRING_ENCRYPTION_H

#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace orknew {

struct StringEncryptionPass : public llvm::PassInfoMixin<StringEncryptionPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

private:
  void encryptGlobalString(llvm::Module &M, llvm::GlobalVariable &GV);
};

} // namespace orknew

#endif
