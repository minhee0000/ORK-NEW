#!/bin/bash
# ORK-NEW Android NDK 통합 설치 스크립트
# 사용법: ./install.sh <android-project-dir>

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KLD_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
KLD_DYLIB="$KLD_ROOT/build/ORK-NEW.dylib"
KLD_RUNTIME="$KLD_ROOT/runtime/kld_runtime.c"
KLD_TOOLCHAIN="$SCRIPT_DIR/ork-new-ndk-toolchain.cmake"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "=== ORK-NEW Android NDK Integration ==="
echo ""

# 1. dylib 확인
if [ ! -f "$KLD_DYLIB" ]; then
    echo -e "${RED}Error: ORK-NEW.dylib not found${NC}"
    echo "먼저 빌드하세요: ninja -C $KLD_ROOT/build"
    exit 1
fi

# 2. 대상 프로젝트
PROJECT_DIR="${1:-.}"
if [ ! -d "$PROJECT_DIR" ]; then
    echo -e "${RED}Error: $PROJECT_DIR 디렉토리가 없습니다${NC}"
    exit 1
fi

# 3. CMake 툴체인 파일 복사
CMAKE_DST="$PROJECT_DIR/ork-new-ndk-toolchain.cmake"
cp "$KLD_TOOLCHAIN" "$CMAKE_DST"
echo -e "${GREEN}✓ CMake toolchain 설치: $CMAKE_DST${NC}"

# 4. 런타임 복사
JNI_DIR="$PROJECT_DIR/app/src/main/jni"
CPP_DIR="$PROJECT_DIR/app/src/main/cpp"

if [ -d "$CPP_DIR" ]; then
    RUNTIME_DST="$CPP_DIR/kld_runtime.c"
elif [ -d "$JNI_DIR" ]; then
    RUNTIME_DST="$JNI_DIR/kld_runtime.c"
else
    RUNTIME_DST="$PROJECT_DIR/kld_runtime.c"
fi

cp "$KLD_RUNTIME" "$RUNTIME_DST"
echo -e "${GREEN}✓ 런타임 복사: $RUNTIME_DST${NC}"

echo ""
echo -e "${YELLOW}=== 사용법 ===${NC}"
echo ""
echo "방법 1: CMakeLists.txt에서 직접 사용"
echo "  app/build.gradle의 cmake 설정에 추가:"
echo "    arguments '-DORK_NEW_LIB_PATH=$KLD_DYLIB'"
echo "  CMakeLists.txt에 추가:"
echo "    set(CMAKE_C_FLAGS \"\${CMAKE_C_FLAGS} -fpass-plugin=$KLD_DYLIB\")"
echo ""
echo "방법 2: Toolchain wrapper 사용"
echo "  arguments '-DCMAKE_TOOLCHAIN_FILE=$CMAKE_DST',"
echo "            '-DANDROID_NDK=\$ANDROID_NDK',"
echo "            '-DORK_NEW_LIB_PATH=$KLD_DYLIB'"
echo ""
echo "난독화 끄기: -DORK_NEW_ENABLED=OFF"
echo "kld_runtime.c를 CMakeLists.txt의 소스 목록에 추가하세요"
