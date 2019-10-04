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

# This should be kept in sync with Envoy itself, we just need to apply this patch
# Remove this once https://boringssl-review.googlesource.com/c/boringssl/+/37804 is in master-with-bazel
http_archive(
    name = "boringssl",
    patches = ["//bazel:boringssl.patch"],
    sha256 = "36049e6cd09b353c83878cae0dd84e8b603ba1a40dcd74e44ebad101fc5c672d",
    strip_prefix = "boringssl-37b57ed537987f1b4c60c60fa1aba20f3a0f6d26",
    urls = ["https://github.com/google/boringssl/archive/37b57ed537987f1b4c60c60fa1aba20f3a0f6d26.tar.gz"],
)

local_repository(
    name = "envoy",
    path = "envoy",
)

local_repository(
    name = "envoy_build_config",
    path = "envoy_build_config",
)

http_file(
    name = "xctestrunner",
    executable = 1,
    sha256 = "9e46d5782a9dc7d40bc93c99377c091886c180b8c4ffb9f79a19a58e234cdb09",
    urls = ["https://github.com/google/xctestrunner/releases/download/0.2.10/ios_test_runner.par"],
)

http_archive(
    name = "build_bazel_rules_apple",
    sha256 = "177888104787d5d3953cfec09e19130e460167d6ef9118c4c0907c41bf0fb13f",
    strip_prefix = "rules_apple-a595f71b94f75d531ebdf8ae31cc8eb1ead6a480",
    urls = ["https://github.com/bazelbuild/rules_apple/archive/a595f71b94f75d531ebdf8ae31cc8eb1ead6a480.tar.gz"],
)

load("@envoy//bazel:api_binding.bzl", "envoy_api_binding")

envoy_api_binding()

load("@envoy//bazel:api_repositories.bzl", "envoy_api_dependencies")

envoy_api_dependencies()

load("@envoy//bazel:repositories.bzl", "envoy_dependencies")

envoy_dependencies()

load("@envoy//bazel:dependency_imports.bzl", "envoy_dependency_imports")

envoy_dependency_imports()

load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies()

load("@io_bazel_rules_go//go:deps.bzl", "go_register_toolchains", "go_rules_dependencies")

http_archive(
    name = "build_bazel_apple_support",
    sha256 = "595a6652d8d65380a3d764826bf1a856a8cc52371bbd961dfcd942fdb14bc133",
    strip_prefix = "apple_support-e16463ef91ed77622c17441f9569bda139d45b18",
    urls = ["https://github.com/bazelbuild/apple_support/archive/e16463ef91ed77622c17441f9569bda139d45b18.tar.gz"],
)

http_archive(
    name = "build_bazel_rules_swift",
    sha256 = "1a0ad44a137bf56df24c5b09daa51abdb92ebb5dbe219fd9ce92e2196676c314",
    strip_prefix = "rules_swift-90995572723ed47cbf2968480db250e97a9f5894",
    urls = ["https://github.com/bazelbuild/rules_swift/archive/90995572723ed47cbf2968480db250e97a9f5894.tar.gz"],
)

load("@build_bazel_apple_support//lib:repositories.bzl", "apple_support_dependencies")

apple_support_dependencies()

load("@build_bazel_rules_apple//apple:repositories.bzl", "apple_rules_dependencies")

apple_rules_dependencies(ignore_version_differences = True)

load("@build_bazel_rules_swift//swift:repositories.bzl", "swift_rules_dependencies")

swift_rules_dependencies()

android_sdk_repository(name = "androidsdk")

android_ndk_repository(name = "androidndk")

http_archive(
    name = "rules_jvm_external",
    sha256 = "db56dbd8e96ab31d1ee57c168d6343949d95f36a21085b33003d03585c4dba44",
    strip_prefix = "rules_jvm_external-fc5bd21820581f342a4119a89bfdf36e79c6c549",
    urls = ["https://github.com/bazelbuild/rules_jvm_external/archive/fc5bd21820581f342a4119a89bfdf36e79c6c549.tar.gz"],
)

load("@rules_jvm_external//:defs.bzl", "maven_install")

maven_install(
    artifacts = [
        # Test artifacts
        "org.assertj:assertj-core:3.9.0",
        "junit:junit:4.12",
        "org.mockito:mockito-inline:2.28.2",
        "org.mockito:mockito-core:2.28.2",
    ],
    repositories = [
        "https://repo1.maven.org/maven2",
        "https://jcenter.bintray.com/",
    ],
)

http_archive(
    name = "io_bazel_rules_kotlin",
    sha256 = "52f88499cdd7db892a500951ea5cbb749245c5635e6da0b80a3b7ad4ea976f31",
    strip_prefix = "rules_kotlin-200802f0525af6e3ff4d50985c4f105e0685b883",  # tag legacy-modded-0_26_1-02
    urls = ["https://github.com/cgruber/rules_kotlin/archive/200802f0525af6e3ff4d50985c4f105e0685b883.tar.gz"],
)

load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kotlin_repositories")

kotlin_repositories()

register_toolchains(":kotlin_toolchain")
