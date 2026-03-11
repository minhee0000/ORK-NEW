#ifndef ORKNEW_CONFIG_H
#define ORKNEW_CONFIG_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include <string>

namespace orknew {

// 파일/함수 단위 난독화 설정
class Config {
public:
  static Config &getInstance();

  // 함수가 난독화 대상인지 확인
  bool shouldObfuscate(llvm::StringRef FuncName) const;

  // 특정 패스가 함수에 적용되어야 하는지 확인
  bool isPassEnabled(llvm::StringRef PassName,
                     llvm::StringRef FuncName) const;

  // 패스가 전역적으로 활성화되어 있는지 확인 (모듈 패스용)
  bool isPassEnabled(llvm::StringRef PassName) const;

  // 설정 파일 로드
  void loadFromFile(llvm::StringRef Path);

  // 함수를 제외 목록에 추가
  void excludeFunction(llvm::StringRef FuncName);

  // 함수를 포함 목록에 추가 (포함 목록이 있으면 이것만 난독화)
  void includeFunction(llvm::StringRef FuncName);

private:
  Config() = default;

  llvm::StringSet<> ExcludedFunctions;
  llvm::StringSet<> IncludedFunctions; // 비어있으면 전부 포함
  llvm::StringSet<> DisabledPasses;    // 전역적으로 비활성화된 패스
};

} // namespace orknew

#endif
