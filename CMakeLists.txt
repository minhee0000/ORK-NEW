cmake_minimum_required(VERSION 3.20)
project(ORK-NEW VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CMAKE_SOURCE_DIR}/include)

add_llvm_pass_plugin(ORK-NEW
  lib/Config.cpp
  lib/StringEncryption.cpp
  lib/ControlFlowFlattening.cpp
  lib/InstructionSubstitution.cpp
  lib/InstructionSplitting.cpp
  lib/ConstantObfuscation.cpp
  lib/SymbolStripping.cpp
  lib/Relocation.cpp
  lib/Registration.cpp
)
