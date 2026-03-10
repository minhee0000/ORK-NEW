#include "Obfuscation/ControlFlowFlattening.h"
#include "Obfuscation/Config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include <random>

using namespace llvm;

namespace orknew {

PreservedAnalyses
ControlFlowFlatteningPass::run(Function &F, FunctionAnalysisManager &AM) {
  // 선언, 아주 작은 함수는 스킵 (작은 함수는 CFF 오버헤드가 보안 이득보다 큼)
  if (F.isDeclaration() || F.size() <= 4)
    return PreservedAnalyses::all();

  if (F.getName().starts_with("__kld_") || F.getName().starts_with("kld"))
    return PreservedAnalyses::all();

  if (!Config::getInstance().isPassEnabled("ControlFlowFlattening", F.getName()))
    return PreservedAnalyses::all();

  flatten(F);
  return PreservedAnalyses::none();
}

// PHI 노드를 alloca/load/store로 변환
static void demotePhiNodes(Function &F) {
  SmallVector<PHINode *, 16> PhiNodes;
  for (auto &BB : F)
    for (auto &I : BB)
      if (auto *PN = dyn_cast<PHINode>(&I))
        PhiNodes.push_back(PN);

  for (auto *PN : PhiNodes)
    DemotePHIToStack(PN);
}

// CFF가 도미넌스를 깨뜨리므로, BB를 넘는 SSA 값을 alloca로 변환
static void demoteCrossBBRegisters(Function &F) {
  SmallVector<Instruction *, 64> ToDemote;

  for (auto &BB : F) {
    for (auto &I : BB) {
      if (I.isTerminator() || isa<AllocaInst>(&I) || I.getType()->isVoidTy())
        continue;
      for (auto *U : I.users()) {
        if (auto *UI = dyn_cast<Instruction>(U)) {
          if (UI->getParent() != &BB) {
            ToDemote.push_back(&I);
            break;
          }
        }
      }
    }
  }
  for (auto *I : ToDemote)
    DemoteRegToStack(*I);
}

// 자동화 예측 공격 방지: opaque predicate 삽입
// 항상 참이지만 정적 분석으로 판별하기 어려운 조건문
// 패턴을 랜덤 선택하여 분석 도구의 패턴 매칭을 방해
static Value *createOpaquePredicate(IRBuilder<> &Builder, Function &F,
                                    std::mt19937 &Rng) {
  auto *Int32Ty = Type::getInt32Ty(F.getContext());
  auto *Alloca = Builder.CreateAlloca(Int32Ty);
  Builder.CreateStore(ConstantInt::get(Int32Ty, Rng()), Alloca);
  Value *X = Builder.CreateLoad(Int32Ty, Alloca);

  int Pattern = Rng() % 3;
  switch (Pattern) {
  case 0: {
    // x*(x+1) % 2 == 0 (연속 정수 곱은 항상 짝수)
    Value *XPlus1 = Builder.CreateAdd(X, ConstantInt::get(Int32Ty, 1));
    Value *Mul = Builder.CreateMul(X, XPlus1);
    Value *Mod = Builder.CreateAnd(Mul, ConstantInt::get(Int32Ty, 1));
    return Builder.CreateICmpEQ(Mod, ConstantInt::get(Int32Ty, 0));
  }
  case 1: {
    // (x^2 + x) % 2 == 0 (x^2+x = x(x+1)이므로 항상 짝수)
    Value *Sq = Builder.CreateMul(X, X);
    Value *Sum = Builder.CreateAdd(Sq, X);
    Value *Mod = Builder.CreateAnd(Sum, ConstantInt::get(Int32Ty, 1));
    return Builder.CreateICmpEQ(Mod, ConstantInt::get(Int32Ty, 0));
  }
  default: {
    // (x | ~x) == -1 (항상 참: 모든 비트가 1)
    Value *NotX = Builder.CreateNot(X);
    Value *OrVal = Builder.CreateOr(X, NotX);
    return Builder.CreateICmpEQ(OrVal, ConstantInt::get(Int32Ty, -1));
  }
  }
}

// cross-BB SSA 값 개수 세기 (CFF 적용 가능 여부 판단용)
static unsigned countCrossBBValues(Function &F) {
  unsigned Count = 0;
  for (auto &BB : F)
    for (auto &I : BB) {
      if (I.isTerminator() || isa<AllocaInst>(&I) || I.getType()->isVoidTy())
        continue;
      for (auto *U : I.users())
        if (auto *UI = dyn_cast<Instruction>(U))
          if (UI->getParent() != &BB) {
            Count++;
            break;
          }
    }
  return Count;
}

void ControlFlowFlatteningPass::flatten(Function &F) {
  // 0. PHI 노드를 alloca로 변환
  demotePhiNodes(F);

  // 1. 엔트리 블록 분리
  BasicBlock &EntryBB = F.getEntryBlock();
  auto SplitIt = EntryBB.begin();
  while (SplitIt != EntryBB.end() && isa<AllocaInst>(&*SplitIt))
    ++SplitIt;
  if (SplitIt == EntryBB.end())
    return;

  BasicBlock *FirstBB = EntryBB.splitBasicBlock(SplitIt, "entry.split");

  // 2. 평탄화 대상 블록 수집
  SmallVector<BasicBlock *, 16> OrigBBs;
  for (auto &BB : F) {
    if (&BB == &EntryBB)
      continue;
    OrigBBs.push_back(&BB);
  }

  if (OrigBBs.size() <= 1)
    return;

  // 3. 각 블록에 밀집 case 번호 할당 (랜덤 순열)
  // 밀집 범위 → LLVM 백엔드가 jump table 생성 → O(1) 디스패치
  std::mt19937 Rng(std::random_device{}());
  SmallVector<uint32_t, 16> CaseValues;
  for (unsigned I = 0; I < OrigBBs.size(); I++)
    CaseValues.push_back(I);
  // Fisher-Yates 셔플로 비결정적 매핑
  for (unsigned I = CaseValues.size() - 1; I > 0; I--) {
    unsigned J = Rng() % (I + 1);
    std::swap(CaseValues[I], CaseValues[J]);
  }
  DenseMap<BasicBlock *, uint32_t> BBToCase;
  for (unsigned I = 0; I < OrigBBs.size(); I++)
    BBToCase[OrigBBs[I]] = CaseValues[I];

  // 4. switch 변수 + XOR 키 생성
  // case 값을 XOR 인코딩하여 정적 분석 도구의 CFG 재구성을 방해
  auto *Int32Ty = Type::getInt32Ty(F.getContext());
  IRBuilder<> EntryBuilder(&EntryBB, EntryBB.begin());
  AllocaInst *SwitchVar =
      EntryBuilder.CreateAlloca(Int32Ty, nullptr, "kld.sw.var");
  uint32_t XorKey = Rng() | 1; // 비결정적 XOR 키 (0이 아닌 값)

  // 인코딩된 초기 case 저장: real_case ^ key
  new StoreInst(
      ConstantInt::get(Int32Ty, BBToCase[FirstBB] ^ XorKey), SwitchVar,
      EntryBB.getTerminator()->getIterator());

  EntryBB.getTerminator()->eraseFromParent();

  // 5. dispatch 블록: load → XOR decode → switch
  BasicBlock *DispatchBB =
      BasicBlock::Create(F.getContext(), "kld.dispatch", &F, FirstBB);
  BasicBlock *DefaultBB =
      BasicBlock::Create(F.getContext(), "kld.default", &F);
  new UnreachableInst(F.getContext(), DefaultBB);

  BranchInst::Create(DispatchBB, &EntryBB);

  IRBuilder<> DispatchBuilder(DispatchBB);
  LoadInst *Load = DispatchBuilder.CreateLoad(Int32Ty, SwitchVar, "kld.sw");
  Load->setVolatile(true); // 최적화기 제거 방지 필수
  // XOR 디코딩: 저장된 (case ^ key)를 원래 case로 복원
  Value *Decoded =
      DispatchBuilder.CreateXor(Load, ConstantInt::get(Int32Ty, XorKey));

  SwitchInst *Switch =
      DispatchBuilder.CreateSwitch(Decoded, DefaultBB, OrigBBs.size());

  // switch case 값은 디코딩된(원래) 값 사용
  for (auto *BB : OrigBBs)
    Switch->addCase(ConstantInt::get(Int32Ty, BBToCase[BB]), BB);

  // 6. 자동화 예측 공격 방지: bogus BB 삽입
  // 가짜 블록을 추가하여 분석기가 실제 경로와 구분하기 어렵게 만듦
  for (int I = 0; I < 3; I++) {
    BasicBlock *BogusBB =
        BasicBlock::Create(F.getContext(), "", &F);
    IRBuilder<> BogusBuilder(BogusBB);
    // opaque predicate로 dispatch나 default로 분기
    Value *Opaque = createOpaquePredicate(BogusBuilder, F, Rng);
    BogusBuilder.CreateCondBr(Opaque, DispatchBB, DefaultBB);
    // 밀집 범위 연속으로 bogus case 할당 (jump table 유지)
    uint32_t BogusCase = OrigBBs.size() + I;
    Switch->addCase(ConstantInt::get(Int32Ty, BogusCase), BogusBB);
  }

  // 7. BB 순서 맵 생성 (백엣지 감지용)
  DenseMap<BasicBlock *, unsigned> BBIndex;
  for (unsigned Idx = 0; Idx < OrigBBs.size(); Idx++)
    BBIndex[OrigBBs[Idx]] = Idx;

  // 8. 각 블록의 terminator 수정
  // 핵심 최적화: 루프 백엣지(현재 BB보다 이전 BB로의 분기)는
  // 디스패치를 거치지 않고 직접 분기 유지 → 루프 성능 보호
  for (auto *BB : OrigBBs) {
    Instruction *Term = BB->getTerminator();
    if (!Term)
      continue;

    unsigned CurIdx = BBIndex[BB];

    if (auto *Br = dyn_cast<BranchInst>(Term)) {
      if (Br->isUnconditional()) {
        BasicBlock *Succ = Br->getSuccessor(0);
        if (!BBToCase.count(Succ))
          continue;
        // 백엣지 감지: successor가 현재보다 앞이면 루프 → 직접 분기 유지
        if (BBIndex.count(Succ) && BBIndex[Succ] <= CurIdx)
          continue;
        new StoreInst(
            ConstantInt::get(Int32Ty, BBToCase[Succ] ^ XorKey), SwitchVar,
            Br->getIterator());
        Br->setSuccessor(0, DispatchBB);
      } else {
        BasicBlock *TrueBB = Br->getSuccessor(0);
        BasicBlock *FalseBB = Br->getSuccessor(1);
        if (!BBToCase.count(TrueBB) || !BBToCase.count(FalseBB))
          continue;

        bool TrueIsBack =
            BBIndex.count(TrueBB) && BBIndex[TrueBB] <= CurIdx;
        bool FalseIsBack =
            BBIndex.count(FalseBB) && BBIndex[FalseBB] <= CurIdx;

        if (TrueIsBack || FalseIsBack) {
          // 조건 분기에서 한쪽이라도 백엣지면 원래 분기 유지 (루프 보호)
          continue;
        }

        IRBuilder<> Builder(Br);
        Value *Cond = Br->getCondition();
        Value *TrueCase =
            ConstantInt::get(Int32Ty, BBToCase[TrueBB] ^ XorKey);
        Value *FalseCase =
            ConstantInt::get(Int32Ty, BBToCase[FalseBB] ^ XorKey);
        Value *Selected = Builder.CreateSelect(Cond, TrueCase, FalseCase);
        new StoreInst(Selected, SwitchVar, /*isVolatile=*/true,
                      Br->getIterator());
        ReplaceInstWithInst(Br, BranchInst::Create(DispatchBB));
      }
    }
  }

}

} // namespace orknew
