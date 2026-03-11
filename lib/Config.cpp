#include "Obfuscation/Config.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

namespace orknew {

Config &Config::getInstance() {
  static Config Instance;
  return Instance;
}

bool Config::shouldObfuscate(llvm::StringRef FuncName) const {
  // __kld_ 런타임 함수는 항상 제외
  if (FuncName.starts_with("__kld_"))
    return false;

  // 제외 목록에 있으면 제외
  if (ExcludedFunctions.count(FuncName))
    return false;

  // 포함 목록이 있으면 포함된 것만
  if (!IncludedFunctions.empty())
    return IncludedFunctions.count(FuncName) > 0;

  return true;
}

bool Config::isPassEnabled(llvm::StringRef PassName,
                           llvm::StringRef FuncName) const {
  if (DisabledPasses.count(PassName))
    return false;
  return shouldObfuscate(FuncName);
}

bool Config::isPassEnabled(llvm::StringRef PassName) const {
  return !DisabledPasses.count(PassName);
}

void Config::loadFromFile(llvm::StringRef Path) {
  auto BufOrErr = llvm::MemoryBuffer::getFile(Path);
  if (!BufOrErr) {
    llvm::errs() << "orknew: cannot open config file: " << Path << "\n";
    return;
  }

  // 간단한 line-based 설정 파서
  // 형식:
  //   exclude: functionName
  //   include: functionName
  //   disable: passName
  llvm::StringRef Content = (*BufOrErr)->getBuffer();
  llvm::SmallVector<llvm::StringRef, 32> Lines;
  Content.split(Lines, '\n');

  for (auto &Line : Lines) {
    llvm::StringRef Trimmed = Line.trim();
    if (Trimmed.empty() || Trimmed.starts_with("#"))
      continue;

    if (Trimmed.starts_with("exclude:")) {
      llvm::StringRef Name = Trimmed.substr(8).trim();
      if (!Name.empty())
        ExcludedFunctions.insert(Name);
    } else if (Trimmed.starts_with("include:")) {
      llvm::StringRef Name = Trimmed.substr(8).trim();
      if (!Name.empty())
        IncludedFunctions.insert(Name);
    } else if (Trimmed.starts_with("disable:")) {
      llvm::StringRef Name = Trimmed.substr(8).trim();
      if (!Name.empty())
        DisabledPasses.insert(Name);
    }
  }
}

void Config::excludeFunction(llvm::StringRef FuncName) {
  ExcludedFunctions.insert(FuncName);
}

void Config::includeFunction(llvm::StringRef FuncName) {
  IncludedFunctions.insert(FuncName);
}

} // namespace orknew
