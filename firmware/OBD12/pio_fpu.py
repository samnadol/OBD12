# PlatformIO pre-script: match STM32CubeIDE's hard-float ABI (fpv4-sp-d16).
# The flags must reach the compiler, the assembler and the linker, otherwise
# the linker picks soft-float newlib and the link fails.
Import("env")

fpu_flags = [
    "-mcpu=cortex-m4",
    "-mthumb",
    "-mfpu=fpv4-sp-d16",
    "-mfloat-abi=hard",
]

env.Append(CCFLAGS=fpu_flags, ASFLAGS=fpu_flags, LINKFLAGS=fpu_flags)
