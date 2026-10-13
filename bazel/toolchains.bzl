load("@envoy_repo//:compiler.bzl", _USE_LIBSTDCPP = "USE_LIBSTDCPP")
load(
    "@llvm_toolchain_llvm//:llvm.bzl",
    _LLVM_IS_HOST = "LLVM_IS_HOST",
    _LLVM_LIB_DIR = "LLVM_LIB_DIR",
    _LLVM_MAJOR = "LLVM_MAJOR",
    _LLVM_MAJOR_MINOR = "LLVM_MAJOR_MINOR",
    _LLVM_VERSION = "LLVM_VERSION",
)

LLVM_IS_HOST = _LLVM_IS_HOST
LLVM_LIB_DIR = _LLVM_LIB_DIR
LLVM_MAJOR = _LLVM_MAJOR
LLVM_MAJOR_MINOR = _LLVM_MAJOR_MINOR
LLVM_VERSION = _LLVM_VERSION
USE_LIBSTDCPP = _USE_LIBSTDCPP

LIBCLANG_CPP = "@llvm_toolchain_llvm//:%s/libclang-cpp.so.%s" % (LLVM_LIB_DIR, LLVM_MAJOR_MINOR)

# Distro-packaged LLVM splits libLLVM.so out of libclang-cpp.so; the hermetic bundle folds it in.
LIBLLVM = ("@llvm_toolchain_llvm//:%s/libLLVM.so.%s" % (LLVM_LIB_DIR, LLVM_MAJOR_MINOR)) if LLVM_IS_HOST else None
