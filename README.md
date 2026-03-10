# ORK-NEW

LLVM-based code obfuscation compiler plugin

Applies multi-layered obfuscation to hinder binary reverse engineering. Built as an out-of-tree plugin on LLVM's New Pass Manager, supporting all Clang-compiled languages (C, C++, Objective-C).

## Obfuscation Passes

| Pass | Description |
|---|---|
| **ControlFlowFlattening** | Flattens control flow into switch dispatch (XOR key encoding + opaque predicates + bogus BBs + chained multi-dispatch for large functions) |
| **StringEncryption** | XOR string encryption + constructor-based in-place decryption (thread-safe) |
| **InstructionSubstitution** | Replaces arithmetic/logic ops with MBA (Mixed Boolean Arithmetic) equivalents |
| **ConstantObfuscation** | Replaces integer constants with runtime expressions (XOR/ADD/SUB/MUL) |
| **InstructionSplitting** | Splits basic blocks to increase CFF complexity |
| **Relocation** | Randomly shuffles function/basic block order |
| **SymbolStripping** | Strips internal symbol names |

## Build

```bash
brew install llvm cmake ninja

mkdir build && cd build
cmake -G Ninja -DLLVM_DIR=$(brew --prefix llvm)/lib/cmake/llvm ..
ninja
```

Output: `build/ORK-NEW.dylib`

## Usage

```bash
# Basic compilation
clang -O1 -fpass-plugin=path/to/ORK-NEW.dylib source.c -o output

# With string encryption (requires runtime linkage)
clang -O1 -fpass-plugin=path/to/ORK-NEW.dylib source.c runtime/kld_runtime.c -o output
```

## Configuration

Per-function and per-pass obfuscation control:

```bash
clang -O1 -fpass-plugin=ORK-NEW.dylib -mllvm -kld-config=config.conf source.c
```

```conf
# Exclude specific functions
exclude: hot_loop_function

# Apply only to specific functions
include: check_password

# Disable specific passes
disable: ControlFlowFlattening
```

## Security Features

- **Non-deterministic obfuscation** — produces different binaries on every build
- **XOR key encoding** — encodes switch case values to prevent static CFG reconstruction
- **Opaque predicates** — 3 pattern types selected randomly, defeating signature-based detection
- **Bogus basic blocks** — fake blocks to confuse analysis paths
- **Chained multi-dispatch** — splits large switch dispatchers into chains, enabling CFF on functions of any size
- **MBA substitution** — mixed boolean arithmetic that resists simple algebraic inversion
- **Loop back-edge preservation** — protects loop performance while applying CFF
- **Anti-hooking** — indirect function pointer tables to hinder hooking

## Platform Integration

- **Xcode**: Run `integration/xcode/install.sh` ([detailed guide](USAGE.md#xcode-integration))
- **Android NDK**: Run `integration/ndk/install.sh` ([detailed guide](USAGE.md#android-ndk-integration))

> Apple Clang does not support `-fpass-plugin`. Use Homebrew LLVM Clang instead.

## Performance

| Code Type | Overhead |
|---|---|
| String processing | 1.1–1.3x |
| General logic | 1.5–4x |
| Loop-heavy | 1.0–2x (back-edge preserved) |
| Bitwise-heavy | Auto-skipped |

For performance-sensitive functions, use `exclude` to skip them, or `disable: ControlFlowFlattening` to remove the largest overhead source.

## License

MIT
