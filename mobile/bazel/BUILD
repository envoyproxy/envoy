load("@bazel_skylib//lib:selects.bzl", "selects")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@rules_java//toolchains:default_java_toolchain.bzl", "default_java_toolchain")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

config_setting(
    name = "exclude_certificates",
    values = {"define": "exclude_certificates=true"},
)

exports_files([
    "pom_template.xml",
    "test_manifest.xml",
])

alias(
    name = "zipper",
    actual = "@bazel_tools//tools/zip:zipper",
)

alias(
    name = "jni",
    actual = "@rules_java//toolchains:jni",
    visibility = ["//library/jni:__pkg__"],
)

alias(
    name = "objc_dummy",
    actual = "@bazel_tools//tools/objc:dummy.c",
    visibility = ["//library/jni:__pkg__"],
)

# autoformat requires that there be no @bazel_tools references outside of //bazel,
# so we alias all of the relevant platforms for determining wheel packaging here
alias(
    name = "darwin_arm64",
    actual = "@bazel_tools//src/conditions:darwin_arm64",
)

alias(
    name = "darwin_x86_64",
    actual = "@bazel_tools//src/conditions:darwin_x86_64",
)

alias(
    name = "darwin",
    actual = "@bazel_tools//src/conditions:darwin",
)

alias(
    name = "linux_aarch64",
    actual = "@bazel_tools//src/conditions:linux_aarch64",
)

alias(
    name = "linux_x86_64",
    actual = "@bazel_tools//src/conditions:linux_x86_64",
)

config_setting(
    name = "exec_linux",
    constraint_values = ["@platforms//os:linux"],
)

config_setting(
    name = "exec_macos",
    constraint_values = ["@platforms//os:macos"],
)

config_setting(
    name = "exec_aarch64",
    constraint_values = ["@platforms//cpu:aarch64"],
)

config_setting(
    name = "exec_x86_64",
    constraint_values = ["@platforms//cpu:x86_64"],
)

selects.config_setting_group(
    name = "exec_linux_aarch64",
    match_all = [
        ":exec_linux",
        ":exec_aarch64",
    ],
)

selects.config_setting_group(
    name = "exec_linux_x86_64",
    match_all = [
        ":exec_linux",
        ":exec_x86_64",
    ],
)

selects.config_setting_group(
    name = "exec_macos_aarch64",
    match_all = [
        ":exec_macos",
        ":exec_aarch64",
    ],
)

selects.config_setting_group(
    name = "exec_macos_x86_64",
    match_all = [
        ":exec_macos",
        ":exec_x86_64",
    ],
)

alias(
    name = "dokka_java_runtime",
    actual = select({
        ":exec_linux_aarch64": "@remotejdk17_linux_aarch64//:jdk",
        ":exec_linux_x86_64": "@remotejdk17_linux//:jdk",
        ":exec_macos_aarch64": "@remotejdk17_macos_aarch64//:jdk",
        ":exec_macos_x86_64": "@remotejdk17_macos//:jdk",
    }),
    visibility = ["//visibility:public"],
)

# Keep JavaBuilder on a concrete JDK 21 runtime: rules_java 9.x's BazelJavaBuilder is compiled
# for JDK 21 bytecode, while Envoy Mobile's JVM test stack does not yet work on JDK 25.
[
    default_java_toolchain(
        name = "envoy_mobile_java_toolchain_" + suffix,
        exec_compatible_with = constraints,
        java_runtime = "@remotejdk21_%s//:jdk" % suffix,
        source_version = "8",
        target_version = "8",
    )
    for suffix, constraints in {
        "linux": [
            "@platforms//os:linux",
            "@platforms//cpu:x86_64",
        ],
        "linux_aarch64": [
            "@platforms//os:linux",
            "@platforms//cpu:aarch64",
        ],
        "macos": [
            "@platforms//os:macos",
            "@platforms//cpu:x86_64",
        ],
        "macos_aarch64": [
            "@platforms//os:macos",
            "@platforms//cpu:aarch64",
        ],
    }.items()
]

# Envoy Mobile's runtime and bootstrap runtime toolchains must use the concrete //:jdk targets,
# not the @rules_java//toolchains:remotejdk_21 alias, because that alias resolves through runtime
# toolchain selection and would self-cycle. Pin them to JDK 21 because rules_java 9.x's
# BazelJavaBuilder is compiled for JDK 21 bytecode, while Robolectric 4.16 / Netty 4.1.x ALPN /
# Mockito still do not work on JDK 25. TODO(phlax): drop this once the test dependencies support
# newer JDK runtimes.
_JDK21_PLATFORMS = {
    "linux": [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    "linux_aarch64": [
        "@platforms//os:linux",
        "@platforms//cpu:aarch64",
    ],
    "macos": [
        "@platforms//os:macos",
        "@platforms//cpu:x86_64",
    ],
    "macos_aarch64": [
        "@platforms//os:macos",
        "@platforms//cpu:aarch64",
    ],
}

[
    toolchain(
        name = "envoy_mobile_java_runtime_toolchain_" + suffix,
        target_compatible_with = constraints,
        toolchain = "@remotejdk21_%s//:jdk" % suffix,
        toolchain_type = "@bazel_tools//tools/jdk:runtime_toolchain_type",
    )
    for suffix, constraints in _JDK21_PLATFORMS.items()
]

[
    toolchain(
        name = "envoy_mobile_java_bootstrap_runtime_toolchain_" + suffix,
        target_compatible_with = constraints,
        toolchain = "@remotejdk21_%s//:jdk" % suffix,
        toolchain_type = "@bazel_tools//tools/jdk:bootstrap_runtime_toolchain_type",
    )
    for suffix, constraints in _JDK21_PLATFORMS.items()
]
