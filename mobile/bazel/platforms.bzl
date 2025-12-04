load("@envoy_toolshed//repository:utils.bzl", "arch_alias")

def envoy_mobile_platforms():
    arch_alias(
        name = "mobile_clang_platform",
        aliases = {
            "amd64": "@envoy_mobile//bazel/platforms/rbe:linux_x64",
        },
    )
