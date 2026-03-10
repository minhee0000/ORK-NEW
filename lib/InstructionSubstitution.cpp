#include "Obfuscation/InstructionSubstitution.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include <random>

using namespace llvm;

namespace orknew {

// a + b → (a ^ b) + 2 * (a & b)
Value *InstructionSubstitutionPass::substituteAdd(BinaryOperator *BO,
                                                  IRBuilder<> &Builder) {
  Value *A = BO->getOperand(0);
  Value *B = BO->getOperand(1);

  Value *XorVal = Builder.CreateXor(A, B);
  Value *AndVal = Builder.CreateAnd(A, B);
  Value *Shl = Builder.CreateShl(AndVal, 1);
  return Builder.CreateAdd(XorVal, Shl);
}

// MBA: a + b → (2*(a | b)) - (a ^ b)
Value *InstructionSubstitutionPass::substituteAddMBA(BinaryOperator *BO,
                                                     IRBuilder<> &Builder) {
  Value *A = BO->getOperand(0);
  Value *B = BO->getOperand(1);

  Value *OrVal = Builder.CreateOr(A, B);
  Value *Shl = Builder.CreateShl(OrVal, 1);
  Value *XorVal = Builder.CreateXor(A, B);
  return Builder.CreateSub(Shl, XorVal);
}

// a - b → a + (-b)
Value *InstructionSubstitutionPass::substituteSub(BinaryOperator *BO,
                                                  IRBuilder<> &Builder) {
  Value *A = BO->getOperand(0);
  Value *B = BO->getOperand(1);

  Value *Neg = Builder.CreateNeg(B);
  return Builder.CreateAdd(A, Neg);
}

// MBA: a - b → (a & ~b) - (~a & b)
Value *InstructionSubstitutionPass::substituteSubMBA(BinaryOperator *BO,
                                                     IRBuilder<> &Builder) {
  Value *A = BO->getOperand(0);
  Value *B = BO->getOperand(1);

  Value *NotA = Builder.CreateNot(A);
  Value *NotB = Builder.CreateNot(B);
  Value *Left = Builder.CreateAnd(A, NotB);
  Value *Right = Builder.CreateAnd(NotA, B);
  return Builder.CreateSub(Left, Right);
}

// a ^ b → (~a & b) | (a & ~b)
Value *InstructionSubstitutionPass::substituteXor(BinaryOperator *BO,
                                                  IRBuilder<> &Builder) {
  Value *A = BO->getOperand(0);
  Value *B = BO->getOperand(1);

  Value *NotA = Builder.CreateNot(A);
  Value *NotB = Builder.CreateNot(B);
  Value *Left = Builder.CreateAnd(NotA, B);
  Value *Right = Builder.CreateAnd(A, NotB);
  return Builder.CreateOr(Left, Right);
}

// a & b → (a ^ ~b) & a  (등가: ~(~a | ~b))
Value *InstructionSubstitutionPass::substituteAnd(BinaryOperator *BO,
                                                  IRBuilder<> &Builder) {
  Value *A = BO->getOperand(0);
  Value *B = BO->getOperand(1);

  Value *NotA = Builder.CreateNot(A);
  Value *NotB = Builder.CreateNot(B);
  Value *OrVal = Builder.CreateOr(NotA, NotB);
  return Builder.CreateNot(OrVal);
}

// a | b → (a ^ b) ^ (a & b)  →  더 복잡하게: (a & ~b) | b
Value *InstructionSubstitutionPass::substituteOr(BinaryOperator *BO,
                                                 IRBuilder<> &Builder) {
  Value *A = BO->getOperand(0);
  Value *B = BO->getOperand(1);

  Value *NotB = Builder.CreateNot(B);
  Value *AndVal = Builder.CreateAnd(A, NotB);
  return Builder.CreateOr(AndVal, B);
}

PreservedAnalyses
InstructionSubstitutionPass::run(Function &F, FunctionAnalysisManager &AM) {
  if (F.isDeclaration() || F.getName().starts_with("__kld_") ||
      F.getName().starts_with("kld"))
    return PreservedAnalyses::all();

  std::mt19937 Rng(std::random_device{}());
  bool Changed = false;

  // 비트연산 비율 검사: 50% 이상이면 스킵 (crc32 등 성능 보호)
  unsigned TotalBinOps = 0, BitwiseBinOps = 0;
  for (auto &BB : F)
    for (auto &I : BB)
      if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
        TotalBinOps++;
        unsigned Op = BO->getOpcode();
        if (Op == Instruction::And || Op == Instruction::Or ||
            Op == Instruction::Xor || Op == Instruction::Shl ||
            Op == Instruction::LShr || Op == Instruction::AShr)
          BitwiseBinOps++;
      }
  if (TotalBinOps > 0 && BitwiseBinOps * 100 / TotalBinOps >= 50)
    return PreservedAnalyses::all();

  // 교체할 명령어를 먼저 수집 (순회 중 수정 방지)
  // CFF 디스패치/기본 블록은 스킵 (디스패치 무결성 보호)
  SmallVector<BinaryOperator *, 32> Targets;
  for (auto &BB : F) {
    StringRef BBName = BB.getName();
    if (BBName.starts_with("kld.dispatch") || BBName.starts_with("kld.default"))
      continue;
    for (auto &I : BB)
      if (auto *BO = dyn_cast<BinaryOperator>(&I))
        Targets.push_back(BO);
  }

  for (auto *BO : Targets) {
    // 15% 확률로 치환 (MBA 추가로 보안 강화)
    if (Rng() % 100 >= 15)
      continue;

    IRBuilder<> Builder(BO);
    Value *Result = nullptr;

    switch (BO->getOpcode()) {
    case Instruction::Add:
      // 50% 확률로 기존/MBA 패턴 선택
      Result = (Rng() % 2 == 0) ? substituteAdd(BO, Builder)
                                : substituteAddMBA(BO, Builder);
      break;
    case Instruction::Sub:
      Result = (Rng() % 2 == 0) ? substituteSub(BO, Builder)
                                : substituteSubMBA(BO, Builder);
      break;
    case Instruction::Xor:
      Result = substituteXor(BO, Builder);
      break;
    case Instruction::And:
      Result = substituteAnd(BO, Builder);
      break;
    case Instruction::Or:
      Result = substituteOr(BO, Builder);
      break;
    default:
      continue;
    }

    if (Result) {
      BO->replaceAllUsesWith(Result);
      BO->eraseFromParent();
      Changed = true;
    }
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace orknew
