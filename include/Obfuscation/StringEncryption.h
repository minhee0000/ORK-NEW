#ifndef ORKNEW_STRING_ENCRYPTION_H
#define ORKNEW_STRING_ENCRYPTION_H

#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace orknew {

struct StringEncryptionPass : public llvm::PassInfoMixin<StringEncryptionPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

private:
  struct EncryptedStringInfo {
    llvm::GlobalVariable *GV;
    uint8_t Key;
    uint64_t Size;
  };

  EncryptedStringInfo encryptGlobalString(llvm::Module &M,
                                          llvm::GlobalVariable &GV);
  void generateDecryptorCtor(llvm::Module &M,
                             llvm::ArrayRef<EncryptedStringInfo> Infos);
};

} // namespace orknew

#endif
