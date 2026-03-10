# ORK-NEW Android NDK CMake Toolchain Wrapper
# 사용법: cmake -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake \
#               -DORK_NEW_TOOLCHAIN=<path>/ork-new-ndk-toolchain.cmake \
#               ...
#
# 또는 직접:
# cmake -DCMAKE_TOOLCHAIN_FILE=<path>/ork-new-ndk-toolchain.cmake \
#        -DANDROID_NDK=<ndk-path> ...

# === 설정 ===
# ORK-NEW.dylib 경로
if(NOT DEFINED ORK_NEW_LIB_PATH)
    # 기본 경로: 이 파일 기준 상대 경로
    get_filename_component(_KLD_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    set(ORK_NEW_LIB_PATH "${_KLD_DIR}/build/ORK-NEW.dylib"
        CACHE FILEPATH "Path to ORK-NEW.dylib")
endif()

# 난독화 활성화 여부
option(ORK_NEW_ENABLED "Enable ORK-NEW obfuscation" ON)

# 설정 파일 경로
set(ORK_NEW_CONFIG "" CACHE FILEPATH "Path to ORK-NEW config file")

# === NDK Toolchain 포함 ===
if(DEFINED ANDROID_NDK)
    include("${ANDROID_NDK}/build/cmake/android.toolchain.cmake")
endif()

# === 난독화 플래그 주입 ===
if(ORK_NEW_ENABLED AND EXISTS "${ORK_NEW_LIB_PATH}")
    set(_KLD_FLAGS "-fpass-plugin=${ORK_NEW_LIB_PATH}")

    if(ORK_NEW_CONFIG AND EXISTS "${ORK_NEW_CONFIG}")
        set(_KLD_FLAGS "${_KLD_FLAGS} -mllvm -kld-config=${ORK_NEW_CONFIG}")
    endif()

    # C/C++ 컴파일러 플래그에 추가
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${_KLD_FLAGS}" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_KLD_FLAGS}" CACHE STRING "" FORCE)

    message(STATUS "ORK-NEW: obfuscation ENABLED")
    message(STATUS "  Plugin: ${ORK_NEW_LIB_PATH}")
    if(ORK_NEW_CONFIG)
        message(STATUS "  Config: ${ORK_NEW_CONFIG}")
    endif()
else()
    if(ORK_NEW_ENABLED)
        message(WARNING "ORK-NEW: plugin not found at ${ORK_NEW_LIB_PATH}")
    else()
        message(STATUS "ORK-NEW: obfuscation DISABLED")
    endif()
endif()
