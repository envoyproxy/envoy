licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

# Aliases for Android ABI names - required for incompatible_enable_android_toolchain_resolution
# Bazel's AAR packaging expects platform names to match Android ABI names
#
# This is a bug in bazel/rules_android and these platform aliases should be removed once it is resolved
#
alias(
    name = "armeabi-v7a",
    actual = "//bazel/platforms:android_armv7",
)

alias(
    name = "arm64-v8a",
    actual = "//bazel/platforms:android_arm64",
)

alias(
    name = "x86",
    actual = "//bazel/platforms:android_x86_32",
)

alias(
    name = "x86_64",
    actual = "//bazel/platforms:android_x86_64",
)
