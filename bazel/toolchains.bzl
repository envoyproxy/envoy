load("@envoy_repo//:compiler.bzl", "LLVM_PATH", "USE_LOCAL_SYSROOT")
load("@envoy_toolshed//repository:utils.bzl", "arch_alias")
load("@rules_shell//shell:repositories.bzl", "rules_shell_dependencies", "rules_shell_toolchains")
load("@toolchains_llvm//toolchain:rules.bzl", "llvm_toolchain")

LLVM_VERSION = "18.1.8"
LLVM_MAJOR = LLVM_VERSION.split(".")[0]
LLVM_MAJOR_MINOR = ".".join(LLVM_VERSION.split(".")[:2])
LIBCLANG_CPP = "@llvm_toolchain_llvm//:lib/libclang-cpp.so." + LLVM_MAJOR_MINOR

def envoy_toolchains():
    native.register_toolchains("@envoy//bazel/rbe/toolchains/configs/linux/gcc/config:cc-toolchain")
    arch_alias(
        name = "clang_platform",
        aliases = {
            "amd64": "@envoy//bazel/platforms/rbe:linux_x64",
            "aarch64": "@envoy//bazel/platforms/rbe:linux_arm64",
        },
    )
    llvm_toolchain(
        name = "llvm_toolchain",
        llvm_version = LLVM_VERSION,
        cxx_cross_lib = {
            "linux-aarch64": "@libcxx_libs_aarch64",
            "linux-x86_64": "@libcxx_libs_x86_64",
        },
        cxx_standard = {"": "c++20"},
        sysroot = {} if USE_LOCAL_SYSROOT else {
            "linux-x86_64": "@sysroot_linux_amd64//:sysroot",
            "linux-aarch64": "@sysroot_linux_arm64//:sysroot",
        },
        toolchain_roots = {"": LLVM_PATH} if LLVM_PATH else {},
    )

    # The same toolchain, for actions that run on Linux arm64 whatever the host is: with
    # `--extra_toolchains=@llvm_toolchain_arm64//:all`, and an arm64 execution platform, an arm64
    # build from an x64 host runs on arm64 workers rather than cross-compiling
    # (`ci/do_ci.sh release.multiarch`). It is only fetched if it is used.
    if not LLVM_PATH:
        llvm_toolchain(
            name = "llvm_toolchain_arm64",
            # The distribution can only be detected for the host.
            distribution = "clang+llvm-%s-aarch64-linux-gnu.tar.xz" % LLVM_VERSION,
            exec_arch = "aarch64",
            exec_os = "linux",
            llvm_version = LLVM_VERSION,
            cxx_cross_lib = {
                "linux-aarch64": "@libcxx_libs_aarch64",
                "linux-x86_64": "@libcxx_libs_x86_64",
            },
            cxx_standard = {"": "c++20"},
            sysroot = {} if USE_LOCAL_SYSROOT else {
                "linux-x86_64": "@sysroot_linux_amd64//:sysroot",
                "linux-aarch64": "@sysroot_linux_arm64//:sysroot",
            },
        )

    rules_shell_dependencies()
    rules_shell_toolchains()
