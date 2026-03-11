#include "Obfuscation/StringEncryption.h"
#include "Obfuscation/Config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <random>

using namespace llvm;

namespace orknew {

PreservedAnalyses StringEncryptionPass::run(Module &M,
                                            ModuleAnalysisManager &AM) {
  if (!Config::getInstance().isPassEnabled("StringEncryption"))
    return PreservedAnalyses::all();

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

  SmallVector<EncryptedStringInfo, 16> EncInfos;
  for (auto *GV : StringGlobals) {
    EncInfos.push_back(encryptGlobalString(M, *GV));
    Changed = true;
  }

  // main() 전에 모든 문자열을 복호화하는 생성자 함수 생성
  if (Changed)
    generateDecryptorCtor(M, EncInfos);

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
  Builder.CreateStore(ConstantInt::get(Int32Ty, Rng()), JunkVar);
  auto *Load = Builder.CreateLoad(Int32Ty, JunkVar, "kld.j");
  cast<LoadInst>(Load)->setVolatile(true);
  // 결과를 사용하지 않으면 제거되므로, dummy xor
  Builder.CreateXor(Load, ConstantInt::get(Int32Ty, Rng()));
}

StringEncryptionPass::EncryptedStringInfo
StringEncryptionPass::encryptGlobalString(Module &M, GlobalVariable &GV) {
  auto *Init = cast<ConstantDataArray>(GV.getInitializer());
  StringRef OrigStr = Init->getAsString();

  // 랜덤 XOR 키 (매번 다른 키)
  std::mt19937 Rng(std::random_device{}());
  uint8_t Key = (Rng() % 254) + 1;

  // 암호화 (인덱스 기반 키 변형 — 런타임 복호화와 동일한 방식)
  std::vector<uint8_t> Encrypted(OrigStr.size());
  for (size_t I = 0; I < OrigStr.size(); I++) {
    uint8_t K = Key ^ static_cast<uint8_t>(I & 0xFF);
    Encrypted[I] = static_cast<uint8_t>(OrigStr[I]) ^ K;
  }

  uint64_t StrSize = OrigStr.size();

  auto *NewInit =
      ConstantDataArray::get(M.getContext(), ArrayRef<uint8_t>(Encrypted));
  GV.setInitializer(NewInit);
  GV.setConstant(false);

  // 인플레이스 복호화 런타임 함수 (원본 데이터를 직접 복호화)
  auto *PtrTy = PointerType::getUnqual(M.getContext());
  auto *Int8Ty = Type::getInt8Ty(M.getContext());
  auto *Int64Ty = Type::getInt64Ty(M.getContext());

  FunctionType *DecFnTy =
      FunctionType::get(PtrTy, {PtrTy, Int8Ty, Int64Ty}, false);
  FunctionCallee DecFn = M.getOrInsertFunction("__kld_decrypt_lazy", DecFnTy);

  // 사용처에 junk 연산 삽입 (정적 분석 방해)
  // 생성자가 이미 복호화하므로 이 호출은 캐시 히트 + 난독화 역할
  SmallVector<User *, 8> Users(GV.users());
  for (auto *U : Users) {
    Instruction *Target = nullptr;

    if (auto *CE = dyn_cast<ConstantExpr>(U)) {
      // ConstantExpr의 Instruction 사용자 중 첫 번째를 찾아 junk 삽입
      for (auto *CEUser : CE->users()) {
        if (auto *I = dyn_cast<Instruction>(CEUser)) {
          if (I->getFunction()) {
            Target = I;
            break;
          }
        }
      }
    } else if (auto *I = dyn_cast<Instruction>(U)) {
      if (I->getFunction())
        Target = I;
    }

    // 사용처에 junk 연산만 삽입 (사용처 교체 없이 — 생성자가 인플레이스 복호화)
    if (Target) {
      IRBuilder<> Builder(Target);
      insertAntiPredictionJunk(Target->getFunction(), Builder, Rng);

      // 더미 decrypt 호출 (캐시 히트, 분석 혼란 용도)
      Builder.CreateCall(
          DecFn, {Builder.CreatePointerCast(&GV, PtrTy), Builder.getInt8(Key),
                  Builder.getInt64(StrSize)});
    }
  }

  return {&GV, Key, StrSize};
}

void StringEncryptionPass::generateDecryptorCtor(
    Module &M, ArrayRef<EncryptedStringInfo> Infos) {
  auto *PtrTy = PointerType::getUnqual(M.getContext());
  auto *Int8Ty = Type::getInt8Ty(M.getContext());
  auto *Int64Ty = Type::getInt64Ty(M.getContext());
  auto *VoidTy = Type::getVoidTy(M.getContext());

  FunctionType *DecFnTy =
      FunctionType::get(PtrTy, {PtrTy, Int8Ty, Int64Ty}, false);
  FunctionCallee DecFn = M.getOrInsertFunction("__kld_decrypt_lazy", DecFnTy);

  // 생성자 함수: main() 전에 실행되어 모든 문자열을 인플레이스 복호화
  FunctionType *CtorTy = FunctionType::get(VoidTy, false);
  Function *Ctor = Function::Create(CtorTy, GlobalValue::InternalLinkage,
                                    "__kld_init_strings", M);
  BasicBlock *BB = BasicBlock::Create(M.getContext(), "entry", Ctor);
  IRBuilder<> Builder(BB);

  for (auto &Info : Infos) {
    Builder.CreateCall(DecFn,
                       {Builder.CreatePointerCast(Info.GV, PtrTy),
                        Builder.getInt8(Info.Key),
                        Builder.getInt64(Info.Size)});
  }
  Builder.CreateRetVoid();

  // llvm.global_ctors에 등록 (우선순위 0 = 가장 먼저 실행)
  appendToGlobalCtors(M, Ctor, 0);
}

} // namespace orknew
