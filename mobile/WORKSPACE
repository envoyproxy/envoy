load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive", "http_file")

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

# TODO: Remove once rules_apple > 0.17.2 is released
http_file(
    name = "xctestrunner",
    executable = 1,
    sha256 = "a3ff412deed453ebe4dc67a98db6ae388b58bd04974d0e862b951089efd26975",
    urls = ["https://github.com/google/xctestrunner/releases/download/0.2.8/ios_test_runner.par"],
)

git_repository(
    name = "build_bazel_rules_apple",
    commit = "7edb4c18fca1514aa6c26fbdf6271625f6823f33",
    remote = "https://github.com/bazelbuild/rules_apple.git",
    shallow_since = "1562886228 -0700",
)

load("@envoy//bazel:api_repositories.bzl", "envoy_api_dependencies")

envoy_api_dependencies()

load("@envoy//bazel:repositories.bzl", "GO_VERSION", "envoy_dependencies")

envoy_dependencies()

load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies()

load("@io_bazel_rules_go//go:deps.bzl", "go_register_toolchains", "go_rules_dependencies")

go_rules_dependencies()

go_register_toolchains(go_version = GO_VERSION)

git_repository(
    name = "build_bazel_apple_support",
    commit = "371f6863768c2cef0e02790a11bdbdc0a39f09fb",
    remote = "https://github.com/bazelbuild/apple_support.git",
    shallow_since = "1560187441 -0700",
)

git_repository(
    name = "build_bazel_rules_swift",
    commit = "770b1fe98693631e72a3b9cd5df4e2ae3d9c76ea",
    remote = "https://github.com/bazelbuild/rules_swift.git",
    shallow_since = "1561403928 -0700",
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

load("@rules_jvm_external//:defs.bzl", "maven_install")

maven_install(
    artifacts = [
        # Test artifacts
        "org.assertj:assertj-core:3.9.0",
        "junit:junit:4.12",
    ],
    repositories = [
        "https://repo1.maven.org/maven2",
        "https://jcenter.bintray.com/",
    ],
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
    shallow_since = "1556831609 -0700",
)

load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kotlin_repositories")

kotlin_repositories()

register_toolchains(":kotlin_toolchain")
