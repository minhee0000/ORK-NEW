#!/bin/bash
# ORK-NEW Xcode 통합 설치 스크립트
# 사용법: ./install.sh <xcode-project-dir>

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KLD_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
KLD_DYLIB="$KLD_ROOT/build/ORK-NEW.dylib"
KLD_RUNTIME="$KLD_ROOT/runtime/kld_runtime.c"

# 색상
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "=== ORK-NEW Xcode Integration ==="
echo ""

# 1. dylib 존재 확인
if [ ! -f "$KLD_DYLIB" ]; then
    echo -e "${RED}Error: ORK-NEW.dylib not found${NC}"
    echo "먼저 빌드하세요: ninja -C $KLD_ROOT/build"
    exit 1
fi

# 2. LLVM clang 확인
LLVM_CLANG="/opt/homebrew/opt/llvm/bin/clang"
if [ ! -f "$LLVM_CLANG" ]; then
    echo -e "${RED}Error: LLVM clang not found${NC}"
    echo "설치: brew install llvm"
    exit 1
fi

# 3. 대상 프로젝트 디렉토리
PROJECT_DIR="${1:-.}"
if [ ! -d "$PROJECT_DIR" ]; then
    echo -e "${RED}Error: $PROJECT_DIR 디렉토리가 없습니다${NC}"
    exit 1
fi

# 4. xcconfig 복사
XCCONFIG_DST="$PROJECT_DIR/ORK-NEW.xcconfig"
cp "$SCRIPT_DIR/ORK-NEW.xcconfig" "$XCCONFIG_DST"

# 경로 업데이트
sed -i '' "s|ORK_NEW_LIB_PATH = .*|ORK_NEW_LIB_PATH = $KLD_DYLIB|" "$XCCONFIG_DST"
sed -i '' "s|ORK_NEW_CLANG_PATH = .*|ORK_NEW_CLANG_PATH = $(dirname $LLVM_CLANG)|" "$XCCONFIG_DST"

echo -e "${GREEN}✓ xcconfig 설치 완료: $XCCONFIG_DST${NC}"

# 5. 런타임 파일 복사
RUNTIME_DST="$PROJECT_DIR/kld_runtime.c"
cp "$KLD_RUNTIME" "$RUNTIME_DST"
echo -e "${GREEN}✓ 런타임 복사 완료: $RUNTIME_DST${NC}"

echo ""
echo -e "${YELLOW}=== 다음 단계 ===${NC}"
echo "1. Xcode에서 프로젝트 열기"
echo "2. Project > Info > Configurations에서 ORK-NEW.xcconfig 선택"
echo "3. kld_runtime.c를 프로젝트 소스에 추가"
echo "4. 빌드하면 자동으로 난독화 적용"
echo ""
echo "난독화 끄기: xcconfig에서 ORK_NEW_ENABLED = NO"
echo "함수 제외:   설정 파일 생성 후 ORK_NEW_CONFIG_PATH 지정"
