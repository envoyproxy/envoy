load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive", "http_file", "http_jar")

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
    strip_prefix = "rules_jvm_external-2.10",
    sha256 = "1bbf2e48d07686707dd85357e9a94da775e1dbd7c464272b3664283c9c716d26",
    url = "https://github.com/bazelbuild/rules_jvm_external/archive/2.10.zip",
)

load("@rules_jvm_external//:defs.bzl", "maven_install")

maven_install(
    artifacts = [
        # Kotlin
        "org.jetbrains.kotlin:kotlin-stdlib-jdk8:1.3.11",

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
    name = "google_bazel_common",
    sha256 = "d8c9586b24ce4a5513d972668f94b62eb7d705b92405d4bc102131f294751f1d",
    strip_prefix = "bazel-common-413b433b91f26dbe39cdbc20f742ad6555dd1e27",
    urls = ["https://github.com/google/bazel-common/archive/413b433b91f26dbe39cdbc20f742ad6555dd1e27.zip"],
)

http_archive(
    name = "io_bazel_rules_kotlin",
    sha256 = "52f88499cdd7db892a500951ea5cbb749245c5635e6da0b80a3b7ad4ea976f31",
    strip_prefix = "rules_kotlin-200802f0525af6e3ff4d50985c4f105e0685b883",  # tag legacy-modded-0_26_1-02
    urls = ["https://github.com/cgruber/rules_kotlin/archive/200802f0525af6e3ff4d50985c4f105e0685b883.tar.gz"],
)

# gRPC java for @rules_proto_grpc
# The current 0.2.0 uses v1.23.0 of gRPC java which has a buggy version of the grpc_java_repositories
# where it tries to bind the zlib and errors out
# The fix went in on this commit:
# https://github.com/grpc/grpc-java/commit/57e7bd394e92015d2891adc74af0eaf9cd347ea8#diff-515bc54a0cbb4b12fb4a7c465758b011L128-L131
http_archive(
    name = "io_grpc_grpc_java",
    sha256 = "8b495f58aaf75138b24775600a062bbdaa754d85f7ab2a47b2c9ecb432836dd1",
    strip_prefix = "grpc-java-1.24.0",
    urls = ["https://github.com/grpc/grpc-java/archive/v1.24.0.tar.gz"],
)

load("@io_grpc_grpc_java//:repositories.bzl", "grpc_java_repositories")

grpc_java_repositories(
    omit_bazel_skylib = True,
    omit_com_google_protobuf = True,
    omit_com_google_protobuf_javalite = True,
    omit_net_zlib = True,
)

load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kotlin_repositories")

kotlin_repositories()

register_toolchains(":kotlin_toolchain")

http_archive(
    name = "rules_proto_grpc",
    sha256 = "1e08cd6c61f893417b14930ca342950f5f22f71f929a38a8c4bbfeae2a80d03e",
    strip_prefix = "rules_proto_grpc-0.2.0",
    urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/archive/0.2.0.tar.gz"],
)

load("@rules_proto_grpc//protobuf:repositories.bzl", "protobuf_repos")

protobuf_repos()

load("@rules_proto_grpc//:repositories.bzl", "rules_proto_grpc_toolchains")

rules_proto_grpc_toolchains()

load("@rules_proto_grpc//java:repositories.bzl", rules_proto_grpc_java_repos = "java_repos")

rules_proto_grpc_java_repos()

http_jar(
    name = "kotlin_dokka",
    url = "https://github.com/Kotlin/dokka/releases/download/0.9.18/dokka-fatjar-0.9.18.jar",
    sha256 = "4c73eee92dd652ea8e2afd7b20732cf863d4938a30f634d12c88fe64def89fd8",
)
