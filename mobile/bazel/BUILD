load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")

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
    actual = "@bazel_tools//tools/jdk:jni",
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
