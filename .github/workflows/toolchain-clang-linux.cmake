include($ENV{IDF_PATH}/tools/cmake/deduplicate_flags.cmake)

# Tell CMake we are building a standard Linux application, not bare-metal
set(CMAKE_SYSTEM_NAME Linux)

set(CMAKE_C_COMPILER /usr/bin/clang)
set(CMAKE_CXX_COMPILER /usr/bin/clang++)
set(CMAKE_ASM_COMPILER /usr/bin/clang)
set(CMAKE_LINKER ld.lld)

set(CMAKE_AR llvm-ar)
set(CMAKE_RANLIB llvm-ranlib)
set(CMAKE_OBJDUMP objdump)

remove_duplicated_flags(" \
                        ${CMAKE_C_FLAGS}"
                        UNIQ_CMAKE_C_FLAGS)
set(CMAKE_C_FLAGS "${UNIQ_CMAKE_C_FLAGS}"
    CACHE STRING "C Compiler Base Flags"
    FORCE)

remove_duplicated_flags(" \
                        ${CMAKE_CXX_FLAGS}"
                        UNIQ_CMAKE_CXX_FLAGS)
set(CMAKE_CXX_FLAGS "${UNIQ_CMAKE_CXX_FLAGS}"
    CACHE STRING "C++ Compiler Base Flags"
    FORCE)

remove_duplicated_flags(" -Xassembler --longcalls \
                        ${CMAKE_ASM_FLAGS}"
                        UNIQ_CMAKE_ASM_FLAGS)
set(CMAKE_ASM_FLAGS "${UNIQ_CMAKE_ASM_FLAGS}"
    CACHE STRING "Assembler Base Flags"
    FORCE)

# Removed -nostartfiles so the C-runtime (_start) links correctly
remove_duplicated_flags("--ld-path=ld -z noexecstack \
                        ${CMAKE_EXE_LINKER_FLAGS}"
                        UNIQ_CMAKE_EXE_LINKER_FLAGS)
set(CMAKE_EXE_LINKER_FLAGS "${UNIQ_CMAKE_EXE_LINKER_FLAGS}"
    CACHE STRING "Linker Base Flags"
    FORCE)
