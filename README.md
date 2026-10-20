# CryptPS5

> **High-Performance Clean-Room High-Level Emulation (HLE), Symbol Relocation Mapper & Static Binary Synthesis Engine**

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-purple.svg)](https://en.cppreference.com/w/cpp/20)
[![Target Platforms](https://img.shields.io/badge/Platform-Windows%20PE%20%7C%20Linux%20SysV-brightgreen.svg)]()
[![Clean Room Status](https://img.shields.io/badge/Compliance-100%25%20Clean%20Room-success.svg)]()

---

## 1. Project Blueprint

**CryptPS5** is an open-source systems engineering framework designed for executable translation, static relinking, and container analysis targeting x86-64 host environments (Windows and Linux). 

Unlike whole-system machine emulators that virtualize low-level hardware or emulate hypervisor layers, CryptPS5 operates via **static binary synthesis and High-Level Emulation (HLE)**:
* **Direct Relinking (`core/relinker`)**: Translates PS5 ELF binaries into native host executables (PE `.exe` on Windows, ELF on Linux), mapping imports directly to host-native runtime libraries.
* **Instruction Lowering**: Dynamically replaces AMD-specific micro-architectural extensions with standardized x86-64 instruction sequences (`--to-intel`).
* **HLE System Libraries (`core/libs/prx`)**: Provides clean-room C++20 reimplementations of PlayStation system modules (`libkernel`, `libc`, `libSceAgcDriver`, `libScePad`, `libSceAudioOut`), intercepting Numeric Identifier (NID) calls and mapping them to native host OS APIs, SDL2, and Vulkan 1.3.
* **Dual-Tier Container Parser (`core/decrypt`)**: An offline inspection engine capable of safely traversing outer archive formats (`SLB2`) and nested partition tables (`0xEEF51454`) without memory amplification.
* **RDNA Shader Recompiler (`core/shader/recompiler`)**: Lowers AMD RDNA GPU bytecodes to an internal SSA IR and emits standard SPIR-V shaders for execution on modern desktop Vulkan drivers.

---

## 2. Absolute Clean-Room Policy

CryptPS5 is developed under a strict clean-room reverse-engineering policy for preservation, interoperability, and educational systems research:

* **ZERO Hardcoded Decryption Keys**: This repository contains no cryptographic keys, seeds, certificates, or proprietary Sony key material.
* **ZERO Copyrighted Binaries**: No proprietary firmware, BIOS images, system libraries (`.prx`), or copyrighted game packages are hosted or distributed within this project.
* **Clean-Room Implementations**: All HLE runtime modules are authored from the ground up using public ABI documentation, standard POSIX/FreeBSD specifications, and original C++ code.
* **User Responsibility**: Users must supply their own legally obtained software and independently dumped configuration files for testing.

---

## 3. Dynamic Key Strategy (`keys.txt`)

To ensure complete legal compliance and keep the repository clean-room, CryptPS5 separates the container parsing engine from cryptographic materials. 

The software dynamically loads keys at runtime from an optional, user-supplied `keys.txt` configuration file. Independent systems researchers can supply keys derived from their own hardware to inspect partition structures locally:

### Key File Specification (`keys.txt`)
```text
# CryptPS5 Dynamic Key Database
# Format: <key_id_hex> <type> <name> <firmware_range> <key_hex_32> [iv_hex_32]
# Types: PUP_KEY, SEGMENT_KEY, CONTENT_KEY, FAKE_SELF_KEY, KERNEL_KEY

00000010 PUP_KEY     pup_retail_table_key  1.00+  0123456789ABCDEF0123456789ABCDEF  00000000000000000000000000000000
00000020 SEGMENT_KEY system_img_seg_key    1.00+  FEDCBA9876543210FEDCBA9876543210  00000000000000000000000000000000
```

When no keys are supplied, the container engine gracefully operates in diagnostic scan mode, verifying archive headers, computing entropy metrics, and reporting partition table bounds.

---

## 4. Architecture & Subsystems

```
CryptPS5/
├── core/
│   ├── decrypt/                               # Dual-Tier Container Analysis Suite
│   │   ├── include/decrypt/
│   │   │   ├── Crypto.hpp                     # AES-128-CBC / AES-XTS / SHA-256 engines
│   │   │   ├── InnerPUP.hpp                   # Sony 0xEEF51454 inner container parser
│   │   │   ├── PFS.hpp                        # PlayStation File System (PFS) parser
│   │   │   ├── PKG.hpp                        # Package container reader
│   │   │   ├── Self.hpp                       # Signed ELF (SELF) header analyzer
│   │   │   └── SLB2.hpp                       # Outer SLB2 firmware container parser
│   │   ├── KeyDatabase.cpp                    # Dynamic keys.txt loader
│   │   └── ps5decrypt.cpp                     # Standalone CLI & streaming pipeline
│   ├── libs/
│   │   └── prx/                               # Native High-Level Emulation Libraries
│   │       ├── libkernel/                     # Threading, memory, and sync primitives
│   │       │   └── DirectMemory/
│   │       │       ├── DirectMemory.hpp       # Direct memory allocator definitions
│   │       │       ├── Export.cpp             # Exported libkernel memory APIs
│   │       │       ├── PosixMemory.cpp        # Posix memory mapping shims
│   │       │       ├── VirtualMemorySim.hpp   # 16 KiB page alignment manager
│   │       │       └── VirtualMemorySim.cpp   # HLE address space simulator
│   │       ├── libSceAgcDriver/               # Vulkan 1.3 translation for AGC command buffers
│   │       ├── libScePad/                     # DualSense SDL2 controller translation
│   │       └── libc/                          # Standard C runtime adaptation
│   ├── relinker/                              # Executable Relinker & PE Patcher
│   │   ├── domain/include/domain/
│   │   │   └── Types.hpp                      # ELF structure and relocation definitions
│   │   └── relinker/
│   │       ├── include/relinker/
│   │       │   └── ElfSymbolMapper.hpp        # Unencrypted ELF symbol & relocation resolver
│   │       └── src/
│   │           └── ElfSymbolMapper.cpp        # x86-64 relocation application engine
│   ├── shader/                                # RDNA GPU Shader Recompiler
│   │   └── recompiler/                        # RDNA to SPIR-V SSA translation pipeline
│   └── tests_hle_verification.cpp             # Clean-room HLE unit & regression test suite
└── tools/                                     # NID audits, convention checks, and stub generators
```

---

## 5. Local Compilation & Build Instructions

CryptPS5 targets standard C++20 and compiles with GCC/Clang or MinGW-w64 without external runtime DLL dependencies.

### Toolchain Prerequisites
* Windows 10/11 x64 or Linux x86-64
* C++20 compliant compiler: `g++ >= 13` or `clang++ >= 16`
* MSYS2 MinGW-w64 (on Windows) or standard GCC package (on Linux)

### Building the Toolset

#### 1. Build the Container Decryptor & Analyzer (`ps5decrypt.exe`)
```powershell
g++ -std=c++20 -O2 -Wall -Wextra -Werror -static -static-libgcc -static-libstdc++ `
    -I core/decrypt/include core/decrypt/ps5decrypt.cpp core/decrypt/KeyDatabase.cpp -o ps5decrypt.exe
```

#### 2. Build the HLE Verification Test Suite (`tests_hle_verification.exe`)
```powershell
g++ -std=c++20 -O2 -Wall -Wextra -Werror -static `
    -I core/libs -I core/relinker/relinker/include -I core/libs/prx/libkernel/DirectMemory `
    core/tests_hle_verification.cpp `
    core/libs/prx/libkernel/DirectMemory/VirtualMemorySim.cpp `
    core/relinker/relinker/src/ElfSymbolMapper.cpp -o tests_hle_verification.exe
```

#### 3. Build the Static Relinker (`relinker.exe`)
```powershell
cmake -B build_relinker -S core/relinker -DCMAKE_BUILD_TYPE=Release
cmake --build build_relinker --config Release
```

---

## 6. Runtime Operations & CLI Reference

### A. Container Inspection & Extraction (`ps5decrypt.exe`)

* **Scan Container Format & Entropy**:
  ```powershell
  .\ps5decrypt.exe --scan <archive.PUP>
  ```

* **Unpack Outer SLB2 Container**:
  ```powershell
  .\ps5decrypt.exe --pup <archive.PUP> --output .\extracted_pup
  ```

* **Inspect & Stream Partition Table**:
  ```powershell
  # Unpacks components to directory using user keys (zero RAM amplification via 4 MiB streaming)
  .\ps5decrypt.exe --pup .\extracted_pup\PS5UPDATE1.PUP --keys keys.txt --output .\decrypted_partitions
  ```

### B. HLE Virtual Memory & Relocation Tests (`tests_hle_verification.exe`)

Run the regression suite to verify 16 KiB page management and x86-64 relocation logic:
```powershell
.\tests_hle_verification.exe
```

Expected Output:
```text
[*] Starting HLE Environment Verification Suite...
[1] Testing VirtualMemorySimulator...
    [+] Allocated 64 KiB at: 0x...
    [+] Query verified: tag='game_primary_heap', bytes=65536
    [+] Protection updated to PROT_READ
    [+] Virtual memory cleanly released.
[2] Testing ElfSymbolResolver...
    [+] Symbol table parsed: found 'sceKernelExit' -> 0x401000
    [+] Relocation applied at offset 0x100: 0x401020
[+] All HLE environment simulation components verified successfully!
```

### C. Static Relinker Engine (`relinker.exe`)

Relink unencrypted development ELFs directly to native host executables:
```powershell
.\relinker.exe --windows --to-intel <input.elf> <output.exe>
```

---

## 7. License & Disclaimer

This project is licensed under the **GNU General Public License version 2 (GPL-2.0)**. See [LICENSE](LICENSE) for details.

*PlayStation, PS5, and DualSense are registered trademarks of Sony Interactive Entertainment Inc. CryptPS5 is an independent open-source research initiative and is neither affiliated with nor endorsed by Sony.*
