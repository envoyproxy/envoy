load("@envoy_repo//:compiler.bzl", "LLVM_PATH")
load("@envoy_toolshed//repository:utils.bzl", "arch_alias")

def envoy_toolchains():
    native.register_toolchains("@envoy//bazel/rbe/toolchains/configs/linux/clang/config:cc-toolchain")
    native.register_toolchains("@envoy//bazel/rbe/toolchains/configs/linux/clang/config:cc-toolchain-arm64")
    native.register_toolchains("@envoy//bazel/rbe/toolchains/configs/linux/gcc/config:cc-toolchain")
    arch_alias(
        name = "clang_platform",
        aliases = {
            "amd64": "@envoy//bazel/platforms/rbe:linux_x64",
            "aarch64": "@envoy//bazel/platforms/rbe:linux_arm64",
        },
    )
