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

// 단일 switch에 넣을 최대 BB 수 — 레지스터 할당기 안정성 보장
static constexpr unsigned MaxCasesPerSwitch = 80;

PreservedAnalyses
ControlFlowFlatteningPass::run(Function &F, FunctionAnalysisManager &AM) {
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
// 반복 수행: 디모션이 새 교차-BB 참조를 만들 수 있으므로 수렴까지 반복
static void demoteCrossBBRegisters(Function &F) {
  for (int Round = 0; Round < 10; Round++) {
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

    if (ToDemote.empty())
      break;

    for (auto *I : ToDemote)
      DemoteRegToStack(*I);
  }
}

// opaque predicate 삽입 (자동화 예측 공격 방지)
static Value *createOpaquePredicate(IRBuilder<> &Builder, Function &F,
                                    std::mt19937 &Rng) {
  auto *Int32Ty = Type::getInt32Ty(F.getContext());
  auto *Alloca = Builder.CreateAlloca(Int32Ty);
  Builder.CreateStore(ConstantInt::get(Int32Ty, Rng()), Alloca);
  Value *X = Builder.CreateLoad(Int32Ty, Alloca);

  int Pattern = Rng() % 3;
  switch (Pattern) {
  case 0: {
    Value *XPlus1 = Builder.CreateAdd(X, ConstantInt::get(Int32Ty, 1));
    Value *Mul = Builder.CreateMul(X, XPlus1);
    Value *Mod = Builder.CreateAnd(Mul, ConstantInt::get(Int32Ty, 1));
    return Builder.CreateICmpEQ(Mod, ConstantInt::get(Int32Ty, 0));
  }
  case 1: {
    Value *Sq = Builder.CreateMul(X, X);
    Value *Sum = Builder.CreateAdd(Sq, X);
    Value *Mod = Builder.CreateAnd(Sum, ConstantInt::get(Int32Ty, 1));
    return Builder.CreateICmpEQ(Mod, ConstantInt::get(Int32Ty, 0));
  }
  default: {
    Value *NotX = Builder.CreateNot(X);
    Value *OrVal = Builder.CreateOr(X, NotX);
    return Builder.CreateICmpEQ(OrVal, ConstantInt::get(Int32Ty, -1));
  }
  }
}

void ControlFlowFlatteningPass::flatten(Function &F) {
  // 0. PHI 노드를 alloca로 변환
  demotePhiNodes(F);

  // 1. 엔트리 블록 분리 (alloca 이후)
  BasicBlock &EntryBB = F.getEntryBlock();
  auto SplitIt = EntryBB.begin();
  while (SplitIt != EntryBB.end() && isa<AllocaInst>(&*SplitIt))
    ++SplitIt;
  if (SplitIt == EntryBB.end())
    return;

  BasicBlock *FirstBB = EntryBB.splitBasicBlock(SplitIt, "entry.split");

  // 2. cross-BB SSA 값을 alloca로 변환
  demoteCrossBBRegisters(F);

  // 3. BB 수집 (demotion이 새 BB를 만들 수 있으므로 다시 수집)
  SmallVector<BasicBlock *, 64> OrigBBs;
  for (auto &BB : F) {
    if (&BB == &EntryBB)
      continue;
    OrigBBs.push_back(&BB);
  }

  if (OrigBBs.size() <= 1)
    return;

  // 4. 각 블록에 밀집 case 번호 할당 (랜덤 순열)
  std::mt19937 Rng(std::random_device{}());
  SmallVector<uint32_t, 64> CaseValues;
  for (unsigned I = 0; I < OrigBBs.size(); I++)
    CaseValues.push_back(I);
  for (unsigned I = CaseValues.size() - 1; I > 0; I--) {
    unsigned J = Rng() % (I + 1);
    std::swap(CaseValues[I], CaseValues[J]);
  }
  DenseMap<BasicBlock *, uint32_t> BBToCase;
  for (unsigned I = 0; I < OrigBBs.size(); I++)
    BBToCase[OrigBBs[I]] = CaseValues[I];

  // 5. switch 변수 + XOR 키 생성
  auto *Int32Ty = Type::getInt32Ty(F.getContext());
  IRBuilder<> EntryBuilder(&EntryBB, EntryBB.begin());
  AllocaInst *SwitchVar =
      EntryBuilder.CreateAlloca(Int32Ty, nullptr, "kld.sw.var");
  uint32_t XorKey = Rng() | 1;

  // 인코딩된 초기 case 저장
  new StoreInst(ConstantInt::get(Int32Ty, BBToCase[FirstBB] ^ XorKey),
                SwitchVar, /*isVolatile=*/true,
                EntryBB.getTerminator()->getIterator());
  EntryBB.getTerminator()->eraseFromParent();

  // 6. 메인 디스패치 블록: load → XOR decode
  BasicBlock *DispatchBB =
      BasicBlock::Create(F.getContext(), "kld.dispatch", &F, FirstBB);
  BranchInst::Create(DispatchBB, &EntryBB);

  IRBuilder<> DispatchBuilder(DispatchBB);
  LoadInst *Load = DispatchBuilder.CreateLoad(Int32Ty, SwitchVar, "kld.sw");
  Load->setVolatile(true);
  Value *Decoded =
      DispatchBuilder.CreateXor(Load, ConstantInt::get(Int32Ty, XorKey));

  // 7. BB들을 case 값 범위로 청크 분할
  unsigned NumChunks =
      (OrigBBs.size() + MaxCasesPerSwitch - 1) / MaxCasesPerSwitch;

  // 각 청크에 속하는 BB 그룹화 (case 값 범위 기준)
  SmallVector<SmallVector<BasicBlock *, 64>, 4> ChunkBBs(NumChunks);
  for (auto *BB : OrigBBs) {
    unsigned ChunkIdx = BBToCase[BB] / MaxCasesPerSwitch;
    ChunkBBs[ChunkIdx].push_back(BB);
  }

  // 8. 체인형 디스패치 구축
  // dispatch → [chunk0 cases, default→sub1]
  // sub1    → [chunk1 cases, default→sub2]
  // ...
  // subN    → [chunkN cases, default→unreachable]
  // 모든 BB가 평탄화됨 — 각 switch는 최대 MaxCasesPerSwitch개 case

  BasicBlock *DefaultBB =
      BasicBlock::Create(F.getContext(), "kld.default", &F);
  new UnreachableInst(F.getContext(), DefaultBB);

  // 서브 디스패치 BB 생성 (chunk 1부터 — chunk 0은 DispatchBB 사용)
  SmallVector<BasicBlock *, 8> SubDispatchBBs;
  for (unsigned C = 1; C < NumChunks; C++) {
    auto *SubBB =
        BasicBlock::Create(F.getContext(), "kld.sub", &F);
    SubDispatchBBs.push_back(SubBB);
  }

  // 각 청크의 switch 생성
  for (unsigned C = 0; C < NumChunks; C++) {
    // 이 청크의 default (fallthrough) 대상
    BasicBlock *Fallthrough;
    if (C + 1 < NumChunks)
      Fallthrough = SubDispatchBBs[C]; // C=0 → SubDispatchBBs[0] = sub1
    else
      Fallthrough = DefaultBB; // 마지막 청크

    SwitchInst *Switch;
    if (C == 0) {
      // 첫 번째 청크: DispatchBB에 switch 생성
      Switch = DispatchBuilder.CreateSwitch(Decoded, Fallthrough,
                                            ChunkBBs[C].size());
    } else {
      // 후속 청크: 서브 디스패치 BB에 switch 생성
      // Decoded는 DispatchBB에서 정의, 여기서도 사용 가능 (dominance 보장)
      IRBuilder<> SubBuilder(SubDispatchBBs[C - 1]);
      Switch = SubBuilder.CreateSwitch(Decoded, Fallthrough,
                                       ChunkBBs[C].size());
    }

    for (auto *BB : ChunkBBs[C])
      Switch->addCase(ConstantInt::get(Int32Ty, BBToCase[BB]), BB);

    // 청크당 bogus BB 1개 삽입
    BasicBlock *BogusBB = BasicBlock::Create(F.getContext(), "", &F);
    IRBuilder<> BogusBuilder(BogusBB);
    Value *Opaque = createOpaquePredicate(BogusBuilder, F, Rng);
    BogusBuilder.CreateCondBr(Opaque, DispatchBB, DefaultBB);
    uint32_t BogusCase = OrigBBs.size() + C;
    Switch->addCase(ConstantInt::get(Int32Ty, BogusCase), BogusBB);
  }

  // 9. BB 순서 맵 (백엣지 감지용)
  DenseMap<BasicBlock *, unsigned> BBIndex;
  for (unsigned Idx = 0; Idx < OrigBBs.size(); Idx++)
    BBIndex[OrigBBs[Idx]] = Idx;

  // 10. 각 블록의 terminator 수정
  // 모든 평탄화 BB는 DispatchBB(체인 최상단)로 점프
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
            /*isVolatile=*/true, Br->getIterator());
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

        if (TrueIsBack || FalseIsBack)
          continue;

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
    } else if (auto *SI = dyn_cast<SwitchInst>(Term)) {
      // SwitchInst 처리: 각 successor를 dispatch를 통하도록 리다이렉트
      // 각 case에 대해 중간 BB 생성 (store case value → dispatch)
      Value *Cond = SI->getCondition();
      BasicBlock *DefDest = SI->getDefaultDest();

      // default에 대한 스토어 BB 생성
      if (BBToCase.count(DefDest)) {
        BasicBlock *StoreBB =
            BasicBlock::Create(F.getContext(), "", &F, DispatchBB);
        new StoreInst(ConstantInt::get(Int32Ty, BBToCase[DefDest] ^ XorKey),
                      SwitchVar, /*isVolatile=*/true, StoreBB);
        BranchInst::Create(DispatchBB, StoreBB);
        SI->setDefaultDest(StoreBB);
      }

      for (auto Case : SI->cases()) {
        BasicBlock *Succ = Case.getCaseSuccessor();
        if (!BBToCase.count(Succ))
          continue;
        BasicBlock *StoreBB =
            BasicBlock::Create(F.getContext(), "", &F, DispatchBB);
        new StoreInst(ConstantInt::get(Int32Ty, BBToCase[Succ] ^ XorKey),
                      SwitchVar, /*isVolatile=*/true, StoreBB);
        BranchInst::Create(DispatchBB, StoreBB);
        Case.setSuccessor(StoreBB);
      }
    }
  }
}

} // namespace orknew
