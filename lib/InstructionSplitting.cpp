#include "Obfuscation/InstructionSplitting.h"
#include "Obfuscation/Config.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <random>

using namespace llvm;

namespace orknew {

PreservedAnalyses InstructionSplittingPass::run(Function &F,
                                                FunctionAnalysisManager &AM) {
  if (F.isDeclaration() || F.getName().starts_with("__kld_") ||
      F.getName().starts_with("kld"))
    return PreservedAnalyses::all();

  if (!Config::getInstance().isPassEnabled("InstructionSplitting", F.getName()))
    return PreservedAnalyses::all();

  std::mt19937 Rng(std::random_device{}());
  bool Changed = false;

  // BB당 최대 1회 분리 (성능 균형: 너무 많이 쪼개면 CFF switch 폭발)
  SmallVector<Instruction *, 32> SplitPoints;
  for (auto &BB : F) {
    if (BB.size() < 10) // 충분히 큰 BB만 분리 (CFF switch 폭발 방지)
      continue;

    // BB 중간 지점에서 1회만 분리
    unsigned TargetIdx = BB.size() / 2;
    unsigned Count = 0;
    for (auto &I : BB) {
      Count++;
      if (I.isTerminator() || isa<PHINode>(&I) || isa<AllocaInst>(&I))
        continue;
      if (Count >= TargetIdx) {
        SplitPoints.push_back(&I);
        break; // BB당 1개만
      }
    }
  }

  // 수집한 지점에서 BB 분리
  for (auto *I : SplitPoints) {
    BasicBlock *BB = I->getParent();
    if (!BB || I == &BB->front() || I->isTerminator())
      continue;
    SplitBlock(BB, I);
    Changed = true;
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace orknew
