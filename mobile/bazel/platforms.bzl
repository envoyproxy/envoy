load("@envoy_toolshed//repository:utils.bzl", "arch_alias")

def envoy_mobile_platforms():
    native.register_toolchains("//third_party/rbe_configs/config:cc-toolchain")
    arch_alias(
        name = "mobile_clang_platform",
        aliases = {
            "amd64": "@envoy_mobile//third_party/rbe_configs/config:platform",
            "aarch64": "@envoy_mobile//third_party/rbe_configs/config:platform",
        },
    )
