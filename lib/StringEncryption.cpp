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

  // 생성자가 인플레이스 복호화하므로 사용처 변경 불필요
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
