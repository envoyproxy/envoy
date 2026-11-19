load("@envoy_repo//:compiler.bzl", "GCC_PATH", "LLVM_PATH", "USE_LOCAL_SYSROOT")
load("@envoy_toolshed//repository:utils.bzl", "arch_alias")
load("@envoy_toolshed//toolchains/gcc:repositories.bzl", "setup_gcc_toolchains")
load("@rules_shell//shell:repositories.bzl", "rules_shell_dependencies", "rules_shell_toolchains")
load("@toolchains_llvm//toolchain:rules.bzl", "llvm_toolchain")

LLVM_VERSION = "18.1.8"
LLVM_MAJOR = LLVM_VERSION.split(".")[0]
LLVM_MAJOR_MINOR = ".".join(LLVM_VERSION.split(".")[:2])
LIBCLANG_CPP = "@llvm_toolchain_llvm//:lib/libclang-cpp.so." + LLVM_MAJOR_MINOR

# With a local LLVM (`BAZEL_LLVM_PATH`) there is no `@llvm_toolchain_llvm` distribution, so the
# tools that are used from it are taken from the local installation: `bin/lld` and `bin/llvm-dwp`
# by the hermetic GCC toolchain (`--config=gcc`), and `readelf` by `//test/exe:pie_test`.
_LLVM_LOCAL_BUILD = """\
package(default_visibility = ["//visibility:public"])

exports_files([
    "bin/lld",
    "bin/llvm-dwp",
])

filegroup(
    name = "readelf",
    srcs = glob(
        ["bin/llvm-readelf"],
        allow_empty = True,
    ),
)
"""

def envoy_toolchains():
    # GCC is opt-in (`--config=gcc`): hermetic, linking with the LLVM toolchain's lld, unless
    # `BAZEL_GCC_PATH` names a host GCC.
    setup_gcc_toolchains(
        dwp = "@llvm_toolchain_llvm//:bin/llvm-dwp",
        host_path = GCC_PATH or None,
        lld = "@llvm_toolchain_llvm//:bin/lld",
    )
    native.register_toolchains(
        "@envoy//bazel/gcc:linux_x86_64_toolchain",
        "@envoy//bazel/gcc:unavailable_linux_toolchain",
        "@envoy//bazel/gcc:unavailable_macos_toolchain",
        "@envoy//bazel/gcc:unavailable_windows_toolchain",
    )
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
            build_file_content = _LLVM_LOCAL_BUILD,
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
