# ORK-NEW Usage Guide

LLVM 기반 코드 난독화 컴파일러 플러그인

## 빌드

### 사전 요구 사항

```bash
brew install llvm cmake ninja
```

### 컴파일

```bash
mkdir build && cd build
cmake -G Ninja -DLLVM_DIR=$(brew --prefix llvm)/lib/cmake/llvm ..
ninja
```

빌드 결과: `build/ORK-NEW.dylib`

## 기본 사용법

```bash
# 컴파일 시 플러그인 로드
clang -O1 -fpass-plugin=path/to/ORK-NEW.dylib -c source.c -o source.o

# 런타임 링크 (문자열 암호화 사용 시 필수)
clang -O1 -fpass-plugin=path/to/ORK-NEW.dylib source.c runtime/kld_runtime.c -o output
```

지원 언어: C, C++, Objective-C (Clang이 컴파일하는 모든 언어)

## 난독화 패스

| 패스 | 설명 |
|---|---|
| StringEncryption | 문자열을 XOR 암호화하고 런타임에 복호화 |
| ControlFlowFlattening | 제어 흐름을 switch 디스패치로 평탄화 |
| InstructionSubstitution | 산술/논리 연산을 등가 복잡 표현으로 치환 |
| ConstantObfuscation | 정수 상수를 런타임 연산식으로 치환 |
| InstructionSplitting | 기본 블록을 분리하여 CFF 복잡도 증가 |
| Relocation | 함수/기본 블록 순서 셔플 |
| SymbolStripping | 내부 심볼 이름 제거/난독화 |

## 설정 파일

설정 파일로 함수별/패스별 난독화를 제어할 수 있습니다.

```bash
clang -O1 -fpass-plugin=ORK-NEW.dylib -mllvm -kld-config=config.conf source.c
```

### 설정 문법

```conf
# 특정 함수 난독화 제외 (성능 민감 함수)
exclude: crc32_simple
exclude: hot_loop_function

# 특정 함수만 난독화 적용
include: check_password
include: verify_license

# 특정 패스 비활성화
disable: ControlFlowFlattening
disable: InstructionSubstitution
```

- `exclude`가 하나라도 있으면: 해당 함수만 제외, 나머지 전부 적용
- `include`가 하나라도 있으면: 해당 함수만 적용, 나머지 전부 제외
- `disable`: 해당 패스를 전체적으로 비활성화

## Xcode 통합

### 설치

```bash
cd integration/xcode
./install.sh /path/to/your/xcode/project
```

### 수동 설정

1. `ORK-NEW.xcconfig`을 프로젝트에 복사
2. `kld_runtime.c`를 프로젝트 소스에 추가
3. Project > Info > Configurations에서 xcconfig 적용
4. xcconfig 내 경로를 실제 빌드 경로로 수정:

```xcconfig
ORK_NEW_ENABLED = YES
ORK_NEW_PLUGIN_PATH = /path/to/build/ORK-NEW.dylib
ORK_NEW_CONFIG_PATH = /path/to/config.conf

// Apple Clang은 -fpass-plugin을 지원하지 않으므로 Homebrew LLVM 사용
CC = /opt/homebrew/opt/llvm/bin/clang
CPLUSPLUS = /opt/homebrew/opt/llvm/bin/clang++

OTHER_CFLAGS = $(inherited) -fpass-plugin=$(ORK_NEW_PLUGIN_PATH)
OTHER_CPLUSPLUSFLAGS = $(inherited) -fpass-plugin=$(ORK_NEW_PLUGIN_PATH)
```

### 주의사항

- Apple Clang은 `-fpass-plugin`을 지원하지 않습니다. Homebrew LLVM Clang을 사용해야 합니다.
- Release 빌드에서만 난독화를 적용하려면 xcconfig에서 `ORK_NEW_ENABLED` 조건을 설정하세요.

## Android NDK 통합

### 설치

```bash
cd integration/ndk
./install.sh /path/to/your/android/project
```

### 수동 설정

1. `ork-new-ndk-toolchain.cmake`를 프로젝트에 복사
2. `kld_runtime.c`를 NDK 소스에 추가
3. CMakeLists.txt에서 툴체인 래퍼 사용:

```cmake
# app/build.gradle.kts
android {
    defaultConfig {
        externalNativeBuild {
            cmake {
                arguments(
                    "-DCMAKE_TOOLCHAIN_FILE=path/to/ork-new-ndk-toolchain.cmake",
                    "-DORK_NEW_PLUGIN_PATH=path/to/ORK-NEW.dylib"
                )
            }
        }
    }
}
```

## 성능 가이드

### 예상 오버헤드

| 코드 유형 | 전체 패스 오버헤드 |
|---|---|
| 문자열 처리 | 1.1~1.3x |
| 일반 로직 | 1.5~4x |
| 루프 중심 | 2~5x |
| 비트 연산 중심 | 3~4x |

### 성능 최적화 팁

1. **성능 민감 함수 제외**: `exclude: hot_function`
2. **특정 패스만 비활성화**: `disable: ControlFlowFlattening` (가장 큰 오버헤드)
3. **보안 필수 함수만 적용**: `include: verify_password`
4. CFF(제어 흐름 평탄화)가 가장 큰 오버헤드 — 루프 백엣지는 자동 보존
5. 비트 연산 위주 함수는 InstructionSubstitution 자동 스킵

## 보안 기능

- **비결정적 난독화**: 매 빌드마다 다른 바이너리 생성
- **Opaque Predicate**: 정적 분석이 어려운 항상-참 조건
- **Bogus Basic Block**: 가짜 기본 블록으로 분석 방해
- **Anti-prediction Junk**: 복호화 호출 전후 가짜 연산 삽입
- **Anti-hooking**: 간접 함수 호출을 통한 후킹 탐지 방해
- **스레드 안전**: pthread 기반 이중 검사 잠금
