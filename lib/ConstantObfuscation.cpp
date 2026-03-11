#include "Obfuscation/ConstantObfuscation.h"
#include "Obfuscation/Config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include <random>

using namespace llvm;

namespace orknew {

// 상수를 복잡한 연산식으로 치환
// 예: 0x73 → ((0xA5 ^ 0x3B) + (0x73 - 0xA5 + 0x3B))
// 매번 다른 연산식 생성 (비결정적)
static Value *obfuscateConstant(IRBuilder<> &Builder, ConstantInt *CI,
                                std::mt19937 &Rng) {
  uint64_t OrigVal = CI->getZExtValue();
  Type *Ty = CI->getType();
  unsigned BitWidth = Ty->getIntegerBitWidth();

  if (BitWidth > 64)
    return nullptr;

  // 여러 변환 패턴 중 랜덤 선택
  int Pattern = Rng() % 4;

  switch (Pattern) {
  case 0: {
    // val → (A ^ B) where A ^ B == val
    uint64_t A = Rng() & ((1ULL << BitWidth) - 1);
    uint64_t B = OrigVal ^ A;
    return Builder.CreateXor(ConstantInt::get(Ty, A), ConstantInt::get(Ty, B));
  }
  case 1: {
    // val → (A + B) where A + B == val
    uint64_t A = Rng() & ((1ULL << BitWidth) - 1);
    uint64_t B = (OrigVal - A) & ((1ULL << BitWidth) - 1);
    return Builder.CreateAdd(ConstantInt::get(Ty, A), ConstantInt::get(Ty, B));
  }
  case 2: {
    // val → (A - B) where A - B == val
    uint64_t B = Rng() & ((1ULL << BitWidth) - 1);
    uint64_t A = (OrigVal + B) & ((1ULL << BitWidth) - 1);
    return Builder.CreateSub(ConstantInt::get(Ty, A), ConstantInt::get(Ty, B));
  }
  case 3: {
    // val → ((A * B) + C) where (A * B + C) == val
    uint64_t A = (Rng() % 7) + 2; // 2~8
    uint64_t Mask = (BitWidth < 64) ? ((1ULL << BitWidth) - 1) : ~0ULL;
    uint64_t Q = OrigVal / A;
    uint64_t C = OrigVal - (Q * A);
    Value *Mul =
        Builder.CreateMul(ConstantInt::get(Ty, A), ConstantInt::get(Ty, Q));
    return Builder.CreateAdd(Mul, ConstantInt::get(Ty, C & Mask));
  }
  }
  return nullptr;
}

PreservedAnalyses ConstantObfuscationPass::run(Function &F,
                                               FunctionAnalysisManager &AM) {
  if (F.isDeclaration() || F.getName().starts_with("__kld_") ||
      F.getName().starts_with("kld"))
    return PreservedAnalyses::all();

  if (!Config::getInstance().isPassEnabled("ConstantObfuscation", F.getName()))
    return PreservedAnalyses::all();

  std::mt19937 Rng(std::random_device{}());
  bool Changed = false;

  // CFF 대상 함수는 ConstOb 스킵 (CFF가 도미넌스를 깨뜨리므로)
  bool WillBeCFF = F.size() > 4 &&
                   Config::getInstance().isPassEnabled("ControlFlowFlattening",
                                                       F.getName());
  if (WillBeCFF)
    return PreservedAnalyses::all();

  // 대상 명령어 수집
  SmallVector<std::pair<Instruction *, unsigned>, 64> Targets;
  for (auto &BB : F) {
    for (auto &I : BB) {
      // 안전한 명령어만 대상: 이진 연산 + 비교
      if (!isa<BinaryOperator>(&I) && !isa<ICmpInst>(&I))
        continue;
      for (unsigned OpIdx = 0; OpIdx < I.getNumOperands(); ++OpIdx) {
        if (auto *CI = dyn_cast<ConstantInt>(I.getOperand(OpIdx))) {
          int64_t Val = CI->getSExtValue();
          if (Val >= -1 && Val <= 1)
            continue;
          // 35% 확률로 치환 (이전 25%에서 증가)
          if (Rng() % 100 < 35)
            Targets.push_back({&I, OpIdx});
        }
      }
    }
  }

  for (auto &[I, OpIdx] : Targets) {
    auto *CI = dyn_cast<ConstantInt>(I->getOperand(OpIdx));
    if (!CI)
      continue;

    IRBuilder<> Builder(I);
    Value *Obfuscated = obfuscateConstant(Builder, CI, Rng);
    if (Obfuscated) {
      I->setOperand(OpIdx, Obfuscated);
      Changed = true;
    }
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace orknew
