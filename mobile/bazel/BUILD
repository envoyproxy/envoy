load("@bazel_skylib//lib:selects.bzl", "selects")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_toolshed//dependency:macros.bzl", "module_deps_json", "module_updater")
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

module_deps_json(
    name = "module_deps",
    lockfile = "//:MODULE.bazel.lock",
    module_file = "//:MODULE.bazel",
)

module_updater(
    name = "update_module",
    bazelrc = "@envoy//:.bazelrc",
    dependencies = ":module_deps",
    module_file = "//:MODULE.bazel",
)

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
    actual = select(
        {
            ":exec_linux_aarch64": "@remotejdk17_linux_aarch64//:jdk",
            ":exec_linux_x86_64": "@remotejdk17_linux//:jdk",
            ":exec_macos_aarch64": "@remotejdk17_macos_aarch64//:jdk",
            ":exec_macos_x86_64": "@remotejdk17_macos//:jdk",
        },
        no_match_error = "Dokka's pinned JDK 17 runtime is only configured for linux/macos x86_64/aarch64 exec platforms",
    ),
    visibility = ["//visibility:public"],
)

# Envoy Mobile pins all Java tooling to concrete JDK 21 runtimes: rules_java 9.x's
# BazelJavaBuilder is compiled for JDK 21 bytecode, while Robolectric 4.17 / Netty 4.1.x ALPN /
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

# Compile toolchains. Two flavours are required:
#
#  - source/target 8 (`envoy_mobile_java_toolchain_*`): selected under the :android /
#    :mobile-android configs in mobile/.bazelrc, which set --java_language_version=8.
#
#  - default language version (`envoy_mobile_java11_toolchain_*`): selected for every other config
#    (cc-tests, coverage, ...). This MUST be registered by the mobile root module: otherwise
#    toolchain resolution falls through to @envoy//bazel:envoy_java_toolchain_default_definition,
#    whose --sun-misc-unsafe-memory-access=allow javabuilder flag is JDK 23+ only and fails
#    against the JDK 21 runtime pinned below.
[
    default_java_toolchain(
        name = "envoy_mobile_java_toolchain_" + suffix,
        exec_compatible_with = constraints,
        java_runtime = "@remotejdk21_%s//:jdk" % suffix,
        source_version = "8",
        target_version = "8",
    )
    for suffix, constraints in _JDK21_PLATFORMS.items()
]

[
    default_java_toolchain(
        name = "envoy_mobile_java11_toolchain_" + suffix,
        exec_compatible_with = constraints,
        java_runtime = "@remotejdk21_%s//:jdk" % suffix,
    )
    for suffix, constraints in _JDK21_PLATFORMS.items()
]

# Runtime and bootstrap runtime toolchains must use the concrete //:jdk targets, not the
# @rules_java//toolchains:remotejdk_21 alias, because that alias resolves through runtime
# toolchain selection and would self-cycle.
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
