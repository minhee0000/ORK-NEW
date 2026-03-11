#include "Obfuscation/Relocation.h"
#include "Obfuscation/Config.h"
#include "llvm/IR/Module.h"
#include <algorithm>
#include <random>
#include <vector>

using namespace llvm;

namespace orknew {

PreservedAnalyses RelocationPass::run(Module &M, ModuleAnalysisManager &AM) {
  if (!Config::getInstance().isPassEnabled("Relocation"))
    return PreservedAnalyses::all();

  std::mt19937 Rng(std::random_device{}());

  // 1. 함수 순서 셔플
  std::vector<Function *> Funcs;
  for (auto &F : M)
    if (!F.isDeclaration())
      Funcs.push_back(&F);

  if (Funcs.size() > 1) {
    std::shuffle(Funcs.begin(), Funcs.end(), Rng);
    for (auto *F : Funcs)
      F->removeFromParent();
    for (auto *F : Funcs)
      M.getFunctionList().push_back(F);
  }

  // 2. BB 내부 순서 셔플 (각 함수 내 기본 블록 재배치)
  for (auto &F : M) {
    if (F.isDeclaration() || F.size() <= 2)
      continue;
    if (F.getName().starts_with("__kld_") || F.getName().starts_with("kld"))
      continue;
    if (!Config::getInstance().shouldObfuscate(F.getName()))
      continue;

    // 엔트리 블록을 제외한 나머지 BB를 셔플
    std::vector<BasicBlock *> BBs;
    bool First = true;
    for (auto &BB : F) {
      if (First) {
        First = false;
        continue;
      }
      BBs.push_back(&BB);
    }

    if (BBs.size() <= 1)
      continue;

    std::shuffle(BBs.begin(), BBs.end(), Rng);
    for (auto *BB : BBs) {
      BB->removeFromParent();
      F.insert(F.end(), BB);
    }
  }

  return PreservedAnalyses::none();
}

} // namespace orknew
