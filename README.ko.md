# ORK-NEW

LLVM 기반 코드 난독화 컴파일러 플러그인

바이너리 역분석을 방해하기 위한 다층 난독화를 적용합니다. LLVM New Pass Manager 기반 out-of-tree 플러그인으로, Clang이 컴파일하는 모든 언어(C, C++, Objective-C)를 지원합니다.

## 난독화 패스

| 패스 | 설명 |
|---|---|
| **ControlFlowFlattening** | 제어 흐름을 switch 디스패치로 평탄화 (XOR 키 인코딩 + opaque predicate + bogus BB + 대형 함수용 체인형 다중 디스패치) |
| **StringEncryption** | 문자열 XOR 암호화 + 생성자 기반 인플레이스 복호화 (스레드 안전) |
| **InstructionSubstitution** | 산술/논리 연산을 MBA(Mixed Boolean Arithmetic) 등가식으로 치환 |
| **ConstantObfuscation** | 정수 상수를 런타임 연산식(XOR/ADD/SUB/MUL)으로 치환 |
| **InstructionSplitting** | 기본 블록 분리로 CFF 복잡도 증가 |
| **Relocation** | 함수/기본 블록 순서 랜덤 셔플 |
| **SymbolStripping** | 내부 심볼 이름 제거 |

## 빌드

```bash
brew install llvm cmake ninja

mkdir build && cd build
cmake -G Ninja -DLLVM_DIR=$(brew --prefix llvm)/lib/cmake/llvm ..
ninja
```

빌드 결과: `build/ORK-NEW.dylib`

## 사용법

```bash
# 기본 컴파일
clang -O1 -fpass-plugin=path/to/ORK-NEW.dylib source.c -o output

# 문자열 암호화 사용 시 런타임 링크 필수
clang -O1 -fpass-plugin=path/to/ORK-NEW.dylib source.c runtime/kld_runtime.c -o output
```

## 설정

함수별/패스별 난독화를 제어할 수 있습니다.

```bash
clang -O1 -fpass-plugin=ORK-NEW.dylib -mllvm -kld-config=config.conf source.c
```

```conf
# 특정 함수 제외
exclude: hot_loop_function

# 특정 함수만 적용
include: check_password

# 특정 패스 비활성화
disable: ControlFlowFlattening
```

## 보안 기능

- **비결정적 난독화** — 매 빌드마다 다른 바이너리 생성
- **XOR 키 인코딩** — switch case 값을 인코딩하여 정적 CFG 재구성 방해
- **Opaque Predicate** — 3종 패턴 랜덤 선택, 시그니처 기반 탐지 방어
- **Bogus Basic Block** — 가짜 기본 블록으로 분석 경로 혼란
- **체인형 다중 디스패치** — 대형 switch를 체인으로 분할, 함수 크기 제한 없이 CFF 적용
- **MBA 연산 치환** — 단순 대수적 역변환이 불가능한 혼합 불리언 산술
- **루프 백엣지 보존** — 루프 성능을 보호하면서 CFF 적용
- **Anti-hooking** — 간접 함수 포인터 테이블 기반 후킹 방해

## 플랫폼 통합

- **Xcode**: `integration/xcode/install.sh` 실행 ([상세 가이드](USAGE.md#xcode-통합))
- **Android NDK**: `integration/ndk/install.sh` 실행 ([상세 가이드](USAGE.md#android-ndk-통합))

> Apple Clang은 `-fpass-plugin`을 지원하지 않습니다. Homebrew LLVM Clang을 사용하세요.

## 성능

| 코드 유형 | 오버헤드 |
|---|---|
| 문자열 처리 | 1.1~1.3x |
| 일반 로직 | 1.5~4x |
| 루프 중심 | 1.0~2x (백엣지 보존) |
| 비트 연산 중심 | 자동 스킵 |

성능 민감 함수는 `exclude`로 제외하거나, `disable: ControlFlowFlattening`으로 가장 큰 오버헤드를 제거할 수 있습니다.

## 라이선스

Apache License 2.0 — 자세한 내용은 [LICENSE](LICENSE) 참조.
