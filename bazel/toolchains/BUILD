load("@rules_cc//cc:defs.bzl", "cc_toolchain")
load(":gcc_aarch64_cross_toolchain_config.bzl", "gcc_aarch64_cross_toolchain_config_rule")

licenses(["notice"])  # Apache 2

platform(
    name = "rbe_ubuntu_clang_platform",
    parents = ["@rbe_ubuntu_clang//config:platform"],
    remote_execution_properties = """
        {PARENT_REMOTE_EXECUTION_PROPERTIES}
        properties: {
          name: "dockerAddCapabilities"
          value: "SYS_PTRACE,NET_RAW,NET_ADMIN"
        }
        properties: {
          name: "dockerNetwork"
          value: "standard"
        }
        """,
)

platform(
    name = "gcc_aarch64_cross",
    constraint_values = [
        "@platforms//cpu:aarch64",
    ],
)

gcc_aarch64_cross_toolchain_config_rule(name = "gcc_aarch64_cross_toolchain_config")

filegroup(
    name = "gcc_aarch64_cross_toolchain_files",
    srcs = glob([
        "aarch64-none-linux-gnu/**",
        "libexec/**",
        "lib/gcc/aarch64-none-linux-gnu/**",
        "include/**",
    ]),
    visibility = ["//visibility:private"],
)

cc_toolchain(
    name = "gcc_aarch64_cross_cc_toolchain",
    all_files = ":gcc_aarch64_cross_toolchain_files",
    compiler_files = ":gcc_aarch64_cross_toolchain_files",
    dwp_files = ":empty",
    linker_files = ":gcc_aarch64_cross_toolchain_files",
    objcopy_files = ":gcc_aarch64_cross_toolchain_files",
    strip_files = ":gcc_aarch64_cross_toolchain_files",
    supports_param_files = 1,
    toolchain_config = ":gcc_aarch64_cross_toolchain_config",
    toolchain_identifier = "aarch64-linux-gnu",
)

toolchain(
    name = "gcc_aarch64_cross_toolchain",
    target_compatible_with = ["@platforms//cpu:aarch64"],
    toolchain = ":gcc_aarch64_cross_cc_toolchain",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)
