load("@envoy_repo//:compiler.bzl", "LLVM_LIB_DIR", "LLVM_PATH", "LLVM_VERSION_LOCAL", "USE_LIBSTDCPP")

_LLVM_VERSION_HERMETIC = "22.1.8"
LLVM_VERSION = LLVM_VERSION_LOCAL if LLVM_VERSION_LOCAL else _LLVM_VERSION_HERMETIC
LLVM_MAJOR = LLVM_VERSION.split(".")[0]
LLVM_MAJOR_MINOR = ".".join(LLVM_VERSION.split(".")[:2])

_LLVM_LIB_PREFIX = LLVM_LIB_DIR if LLVM_PATH else "lib"
LIBCLANG_CPP = "@llvm_toolchain_llvm//:" + _LLVM_LIB_PREFIX + "/libclang-cpp.so." + LLVM_MAJOR_MINOR

# On distro-packaged LLVM, libclang-cpp.so dynamically links against libLLVM.so
# (they're split). The hermetic LLVM bundles everything into libclang-cpp.so.
LIBLLVM = ("@llvm_toolchain_llvm//:" + _LLVM_LIB_PREFIX + "/libLLVM.so." + LLVM_MAJOR_MINOR) if LLVM_PATH else None
