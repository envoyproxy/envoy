load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# TODO remove once https://github.com/bazelbuild/rules_foreign_cc/pull/253 is resolved
# NOTE: this version should be kept up to date with https://github.com/lyft/envoy-edge-fork/blob/3573b07af1ab5c4cf687ced0f80e2ccc0a0b7ec2/bazel/repository_locations.bzl#L225-L230 until this is removed
http_archive(
    name = "rules_foreign_cc",
    patches = ["//bazel:ranlib.patch"],
    sha256 = "e1b67e1fda647c7713baac11752573bfd4c2d45ef09afb4d4de9eb9bd4e5ac76",
    strip_prefix = "rules_foreign_cc-8648b0446092ef2a34d45b02c8dc4c35c3a8df79",
    urls = ["https://github.com/bazelbuild/rules_foreign_cc/archive/8648b0446092ef2a34d45b02c8dc4c35c3a8df79.tar.gz"],
)

load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")

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
    commit = "2f20f88d85c0fe217cf9a9eadfb8015b3a384dea",
    remote = "https://github.com/bazelbuild/rules_apple.git",
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
    commit = "dabf718975760b4d45bd7db3e18bc12e7b4ef485",
    remote = "https://github.com/bazelbuild/apple_support.git",
    shallow_since = "1551297696 -0500",
)

git_repository(
    name = "build_bazel_rules_swift",
    commit = "e517571f92fe7739635c6e25b2be1f4f185fce33",
    remote = "https://github.com/bazelbuild/rules_swift.git",
    shallow_since = "1551797865 -0800",
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
