#include "Obfuscation/StringEncryption.h"
#include "Obfuscation/Config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include <random>

using namespace llvm;

namespace orknew {

PreservedAnalyses StringEncryptionPass::run(Module &M,
                                            ModuleAnalysisManager &AM) {
  bool Changed = false;

  SmallVector<GlobalVariable *, 16> StringGlobals;
  for (auto &GV : M.globals()) {
    if (!GV.hasInitializer() || !GV.isConstant())
      continue;
    if (auto *Init = dyn_cast<ConstantDataArray>(GV.getInitializer())) {
      if (Init->isString())
        StringGlobals.push_back(&GV);
    }
  }

  for (auto *GV : StringGlobals) {
    encryptGlobalString(M, *GV);
    Changed = true;
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

// 복원 코드 예측 공격 방지: 복호화 호출 전후에 가짜 연산 삽입
// 엔트리 블록에 alloca를 배치하여 IR 규칙 준수
static void insertAntiPredictionJunk(Function *F, IRBuilder<> &Builder,
                                     std::mt19937 &Rng) {
  auto *Int32Ty = Type::getInt32Ty(F->getContext());

  // alloca는 엔트리 블록에 삽입
  IRBuilder<> AllocaBuilder(&F->getEntryBlock(),
                            F->getEntryBlock().getFirstInsertionPt());
  auto *JunkVar = AllocaBuilder.CreateAlloca(Int32Ty, nullptr, "kld.junk");

  // 현재 위치에 volatile store/load 삽입
  auto *Store =
      Builder.CreateStore(ConstantInt::get(Int32Ty, Rng()), JunkVar);
  auto *Load = Builder.CreateLoad(Int32Ty, JunkVar, "kld.j");
  cast<LoadInst>(Load)->setVolatile(true);
  // 결과를 사용하지 않으면 제거되므로, dummy xor
  Builder.CreateXor(Load, ConstantInt::get(Int32Ty, Rng()));
}

void StringEncryptionPass::encryptGlobalString(Module &M,
                                               GlobalVariable &GV) {
  auto *Init = cast<ConstantDataArray>(GV.getInitializer());
  StringRef OrigStr = Init->getAsString();

  // 랜덤 XOR 키 (매번 다른 키)
  std::mt19937 Rng(std::random_device{}());
  uint8_t Key = (Rng() % 254) + 1;

  // 암호화
  std::vector<uint8_t> Encrypted(OrigStr.size());
  for (size_t I = 0; I < OrigStr.size(); I++)
    Encrypted[I] = static_cast<uint8_t>(OrigStr[I]) ^ Key;

  auto *NewInit =
      ConstantDataArray::get(M.getContext(), ArrayRef<uint8_t>(Encrypted));
  GV.setInitializer(NewInit);
  GV.setConstant(false);

  // 힙 기반 지연 복호화 런타임 함수
  auto *PtrTy = PointerType::getUnqual(M.getContext());
  auto *Int8Ty = Type::getInt8Ty(M.getContext());
  auto *Int64Ty = Type::getInt64Ty(M.getContext());

  FunctionType *DecFnTy =
      FunctionType::get(PtrTy, {PtrTy, Int8Ty, Int64Ty}, false);
  FunctionCallee DecFn = M.getOrInsertFunction("__kld_decrypt_lazy", DecFnTy);

  // 사용처 교체
  SmallVector<User *, 8> Users(GV.users());
  for (auto *U : Users) {
    if (auto *CE = dyn_cast<ConstantExpr>(U)) {
      SmallVector<User *, 4> CEUsers(CE->users());
      for (auto *CEUser : CEUsers) {
        if (auto *I = dyn_cast<Instruction>(CEUser)) {
          IRBuilder<> Builder(I);

          // 복원 코드 예측 공격 방지: junk 연산 삽입
          insertAntiPredictionJunk(I->getFunction(), Builder, Rng);

          Value *DecPtr = Builder.CreateCall(
              DecFn,
              {Builder.CreatePointerCast(&GV, PtrTy), Builder.getInt8(Key),
               Builder.getInt64(OrigStr.size())});

          if (CE->getOpcode() == Instruction::GetElementPtr) {
            SmallVector<Value *, 4> Indices;
            for (unsigned Idx = 1; Idx < CE->getNumOperands(); ++Idx)
              Indices.push_back(CE->getOperand(Idx));
            Value *NewGEP = Builder.CreateInBoundsGEP(Int8Ty, DecPtr, Indices);
            I->replaceUsesOfWith(CE, NewGEP);
          } else {
            I->replaceUsesOfWith(CE, DecPtr);
          }
        }
      }
    } else if (auto *I = dyn_cast<Instruction>(U)) {
      IRBuilder<> Builder(I);

      insertAntiPredictionJunk(I->getFunction(), Builder, Rng);

      Value *DecPtr = Builder.CreateCall(
          DecFn, {Builder.CreatePointerCast(&GV, PtrTy), Builder.getInt8(Key),
                  Builder.getInt64(OrigStr.size())});
      I->replaceUsesOfWith(&GV, DecPtr);
    }
  }
}

} // namespace orknew
