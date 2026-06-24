load("@envoy_repo//:compiler.bzl", "LLVM_LIB_DIR", "LLVM_PATH", "LLVM_VERSION_LOCAL", "USE_LIBSTDCPP", "USE_LOCAL_SYSROOT")
load("@envoy_toolshed//repository:utils.bzl", "arch_alias")
load("@toolchains_llvm//toolchain:rules.bzl", "llvm_toolchain")

_LLVM_VERSION_HERMETIC = "18.1.8"
LLVM_VERSION = LLVM_VERSION_LOCAL if LLVM_VERSION_LOCAL else _LLVM_VERSION_HERMETIC
LLVM_MAJOR = LLVM_VERSION.split(".")[0]
LLVM_MAJOR_MINOR = ".".join(LLVM_VERSION.split(".")[:2])

_LLVM_LIB_PREFIX = LLVM_LIB_DIR if LLVM_PATH else "lib"
LIBCLANG_CPP = "@llvm_toolchain_llvm//:" + _LLVM_LIB_PREFIX + "/libclang-cpp.so." + LLVM_MAJOR_MINOR

# On distro-packaged LLVM, libclang-cpp.so dynamically links against libLLVM.so
# (they're split). The hermetic LLVM bundles everything into libclang-cpp.so.
LIBLLVM = ("@llvm_toolchain_llvm//:" + _LLVM_LIB_PREFIX + "/libLLVM.so." + LLVM_MAJOR_MINOR) if LLVM_PATH else None

_LLVM_LOCAL_BUILD = """\
package(default_visibility = ["//visibility:public"])

exports_files([
    "{lib_dir}/libclang-cpp.so.{major_minor}",
    "{lib_dir}/libLLVM.so.{major_minor}",
    "lib/clang/{major}/include/fuzzer/FuzzedDataProvider.h",
])

filegroup(
    name = "include",
    srcs = glob(
        ["lib/clang/*/include/**"],
        allow_empty = True,
    ),
)

filegroup(
    name = "all_includes",
    srcs = [],
)

filegroup(
    name = "symbolizer",
    srcs = ["bin/llvm-symbolizer"],
)
"""

def envoy_toolchains():
    native.register_toolchains("@envoy//bazel/rbe/toolchains/configs/linux/gcc/config:cc-toolchain")
    arch_alias(
        name = "clang_platform",
        aliases = {
            "amd64": "@envoy//bazel/platforms/rbe:linux_x64",
            "aarch64": "@envoy//bazel/platforms/rbe:linux_arm64",
        },
    )

    if LLVM_PATH and "llvm_toolchain_llvm" not in native.existing_rules():
        native.new_local_repository(
            name = "llvm_toolchain_llvm",
            path = LLVM_PATH,
            build_file_content = _LLVM_LOCAL_BUILD.format(
                lib_dir = LLVM_LIB_DIR,
                major = LLVM_MAJOR,
                major_minor = LLVM_MAJOR_MINOR,
            ),
        )

    llvm_toolchain(
        name = "llvm_toolchain",
        llvm_version = LLVM_VERSION,
        cxx_cross_lib = {} if LLVM_PATH else {
            "linux-aarch64": "@libcxx_libs_aarch64",
            "linux-x86_64": "@libcxx_libs_x86_64",
        },
        cxx_standard = {"": "c++20"},
        stdlib = {"": "stdc++"} if USE_LIBSTDCPP else {},
        sysroot = {} if USE_LOCAL_SYSROOT else {
            "linux-x86_64": "@sysroot_linux_amd64//:sysroot",
            "linux-aarch64": "@sysroot_linux_arm64//:sysroot",
        },
        toolchain_roots = {"": LLVM_PATH} if LLVM_PATH else {},
    )
