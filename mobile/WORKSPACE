load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# NOTE: this version should be kept up to date with Envoy upstream:
# https://github.com/envoyproxy/envoy/blob/master/bazel/repository_locations.bzl#L217
# It currently diverges due to known issues with the android NDK and libevent.
# https://github.com/lyft/envoy-mobile/issues/116
http_archive(
    name = "rules_foreign_cc",
    sha256 = "c95ef9c19b713fc25fc0851f81c486e0d55ab90ead67363958327c053d2b108b",
    strip_prefix = "rules_foreign_cc-2b40a0098d4016f620c2ee4c10da0f46f5c90d57",
    # 2019-06-17
    urls = ["https://github.com/bazelbuild/rules_foreign_cc/archive/2b40a0098d4016f620c2ee4c10da0f46f5c90d57.tar.gz"],
)

load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")

# Patch upstream Abseil to prevent Foundation dependency from leaking into Android builds.
# Workaround for https://github.com/abseil/abseil-cpp/issues/326.
# TODO: Should be removed in https://github.com/lyft/envoy-mobile/issues/136 once rules_android
# supports platform toolchains.
http_archive(
    name = "com_google_absl",
    patches = ["//bazel:abseil.patch"],
    sha256 = "7ddf863ddced6fa5bf7304103f9c7aa619c20a2fcf84475512c8d3834b9d14fa",
    strip_prefix = "abseil-cpp-61c9bf3e3e1c28a4aa6d7f1be4b37fd473bb5529",
    urls = ["https://github.com/abseil/abseil-cpp/archive/61c9bf3e3e1c28a4aa6d7f1be4b37fd473bb5529.tar.gz"],
)

local_repository(
    name = "envoy",
    path = "envoy",
)

local_repository(
    name = "envoy_build_config",
    path = "envoy_build_config",
)

git_repository(
    name = "build_bazel_rules_apple",
    commit = "ff6a37b24fcbbd525a5bf61692a12c810d0ee3c1",
    remote = "https://github.com/bazelbuild/rules_apple.git",
    shallow_since = "1559833568 -0700",
)

load("@envoy//bazel:api_repositories.bzl", "envoy_api_dependencies")

envoy_api_dependencies()

load("@envoy//bazel:repositories.bzl", "GO_VERSION", "envoy_dependencies")
load("@envoy//bazel:cc_configure.bzl", "cc_configure")

envoy_dependencies()

load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies()

cc_configure()

load("@io_bazel_rules_go//go:deps.bzl", "go_register_toolchains", "go_rules_dependencies")

go_rules_dependencies()

go_register_toolchains(go_version = GO_VERSION)

git_repository(
    name = "build_bazel_apple_support",
    commit = "bdcef226ad626bd8b9a4a377347a2f8c1726f3bb",
    remote = "https://github.com/bazelbuild/apple_support.git",
    shallow_since = "1554146182 -0700",
)

git_repository(
    name = "build_bazel_rules_swift",
    commit = "c935de3d04a8d24feb09a57df3b33a328be5d863",
    remote = "https://github.com/bazelbuild/rules_swift.git",
    shallow_since = "1559570694 -0700",
)

load("@build_bazel_apple_support//lib:repositories.bzl", "apple_support_dependencies")

apple_support_dependencies()

load("@build_bazel_rules_apple//apple:repositories.bzl", "apple_rules_dependencies")

apple_rules_dependencies(ignore_version_differences = True)

load("@build_bazel_rules_swift//swift:repositories.bzl", "swift_rules_dependencies")

swift_rules_dependencies()

android_sdk_repository(name = "androidsdk")

android_ndk_repository(name = "androidndk")

git_repository(
    name = "rules_jvm_external",
    commit = "fc5bd21820581f342a4119a89bfdf36e79c6c549",
    remote = "https://github.com/bazelbuild/rules_jvm_external.git",
    shallow_since = "1552938175 -0400",
)

# Bazel Kotlin 1.3 patch: https://github.com/bazelbuild/rules_kotlin/issues/159
# bazelbuild/rules_kotlin currently doesn't work with Kotlin 1.3
# TODO: https://github.com/lyft/envoy-mobile/issues/68
#
# keith/rules_kotlin is licensed under the Apache License 2.0
# https://github.com/keith/rules_kotlin/blob/master/LICENSE
git_repository(
    name = "io_bazel_rules_kotlin",
    commit = "af3dea0853f2821e7ece6e028fad57bcd6ce2831",  # from branch ks/bazel-fixups
    remote = "https://github.com/keith/rules_kotlin.git",
    shallow_since = "1544305265 -0800",
)

load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kotlin_repositories")

kotlin_repositories()

register_toolchains(":kotlin_toolchain")
