# PlatformIO pre-script: flags that build_flags does not pass on to every
# step it needs to reach.
Import("env")

# match STM32CubeIDE's hard-float ABI (fpv4-sp-d16); the flags must reach the
# compiler, the assembler and the linker, otherwise the linker picks
# soft-float newlib and the link fails
fpu_flags = [
    "-mcpu=cortex-m4",
    "-mthumb",
    "-mfpu=fpv4-sp-d16",
    "-mfloat-abi=hard",
]

env.Append(CCFLAGS=fpu_flags, ASFLAGS=fpu_flags, LINKFLAGS=fpu_flags)

# newlib-nano: build_flags only hands --specs to the compiler, so without this
# the link pulls in full newlib (about 20 KB more)
env.Append(LINKFLAGS=["--specs=nano.specs"])

# link-time optimisation; the link step optimises too, so it needs -Os again
env.Append(CCFLAGS=["-flto"], LINKFLAGS=["-flto", "-Os"])

# The syscalls (syscalls.c, sysmem.c, Comms/log.c) are only referenced from
# libc, which is linked after LTO has dropped everything it saw no use for.
env.Append(LINKFLAGS=["-Wl,-u," + name for name in (
    "_close", "_exit", "_fstat", "_getpid", "_isatty", "_kill", "_lseek",
    "_read", "_sbrk", "_write",
)])
