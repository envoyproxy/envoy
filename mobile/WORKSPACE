load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive", "http_file", "http_jar")

# Patch protobuf to prevent duplicate symbols: https://github.com/lyft/envoy-mobile/issues/617
# More details: https://github.com/protocolbuffers/protobuf/issues/7046
# TODO: Remove after https://github.com/bazelbuild/bazel/pull/10493 is merged to Bazel
# Reverts:
# - https://github.com/protocolbuffers/protobuf/commit/7b28278c7d4f4175e70aef2f89d304696eb85ae3
# - https://github.com/protocolbuffers/protobuf/commit/a03d332aca5d33c5d4b2cd25037c9e37d57eff02
http_archive(
    name = "com_google_protobuf",
    patch_args = ["-p1"],
    patches = [
        "@envoy//bazel:protobuf.patch",
        "//bazel:protobuf.patch",
    ],
    sha256 = "d7cfd31620a352b2ee8c1ed883222a0d77e44346643458e062e86b1d069ace3e",
    strip_prefix = "protobuf-3.10.1",
    urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v3.10.1/protobuf-all-3.10.1.tar.gz"],
)

# Patch upstream Abseil to prevent Foundation dependency from leaking into Android builds.
# Workaround for https://github.com/abseil/abseil-cpp/issues/326.
# TODO: Should be removed in https://github.com/lyft/envoy-mobile/issues/136 once rules_android
# supports platform toolchains.
http_archive(
    name = "com_google_absl",
    patches = ["//bazel:abseil.patch"],
    sha256 = "14ee08e2089c2a9b6bf27e1d10abc5629c69c4d0bab4b78ec5b65a29ea1c2af7",
    strip_prefix = "abseil-cpp-cf3a1998e9d41709d4141e2f13375993cba1130e",
    # 2020-03-05
    urls = ["https://github.com/abseil/abseil-cpp/archive/cf3a1998e9d41709d4141e2f13375993cba1130e.tar.gz"],
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
    sha256 = "0338c71977106f1304a8056739db6f462a76f386a299052c1ed7f8fd463d01a8",
    urls = ["https://github.com/google/xctestrunner/releases/download/0.2.11/ios_test_runner.par"],
)

http_archive(
    name = "build_bazel_rules_apple",
    sha256 = "ee9e6073aeb5a65c100cb9c44b0017c937706a4ae03176e14a7e78620a198079",
    strip_prefix = "rules_apple-5131f3d46794bf227d296c82f30c2499c9de3c5b",
    url = "https://github.com/bazelbuild/rules_apple/archive/5131f3d46794bf227d296c82f30c2499c9de3c5b.tar.gz",
)

http_archive(
    name = "build_bazel_rules_swift",
    sha256 = "d0833bc6dad817a367936a5f902a0c11318160b5e80a20ece35fb85a5675c886",
    strip_prefix = "rules_swift-3eeeb53cebda55b349d64c9fc144e18c5f7c0eb8",
    url = "https://github.com/bazelbuild/rules_swift/archive/3eeeb53cebda55b349d64c9fc144e18c5f7c0eb8.tar.gz",
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
    sha256 = "1bbf2e48d07686707dd85357e9a94da775e1dbd7c464272b3664283c9c716d26",
    strip_prefix = "rules_jvm_external-2.10",
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

rules_kotlin_version = "legacy-1.3.0-rc2"

rules_kotlin_sha = "dc1c76f91228ddaf4f7ca4190b82d61939e95369f61dea715e8be28792072b1b"

http_archive(
    name = "io_bazel_rules_kotlin",
    sha256 = rules_kotlin_sha,
    strip_prefix = "rules_kotlin-%s" % rules_kotlin_version,
    type = "zip",
    urls = ["https://github.com/bazelbuild/rules_kotlin/archive/%s.zip" % rules_kotlin_version],
)

load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kotlin_repositories", "kt_register_toolchains")

kotlin_repositories()

kt_register_toolchains()

rules_detekt_version = "0.3.0"

rules_detekt_sha = "b1b4c8a3228f880a169ab60a817619bc4cf254443196e7e108ece411cb9c580e"

http_archive(
    name = "rules_detekt",
    sha256 = rules_detekt_sha,
    strip_prefix = "bazel_rules_detekt-{v}".format(v = rules_detekt_version),
    url = "https://github.com/buildfoundation/bazel_rules_detekt/archive/v{v}.tar.gz".format(v = rules_detekt_version),
)

load("@rules_detekt//detekt:dependencies.bzl", "rules_detekt_dependencies")

rules_detekt_dependencies()

load("@rules_detekt//detekt:toolchains.bzl", "rules_detekt_toolchains")

rules_detekt_toolchains(detekt_version = "1.8.0")

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
    sha256 = "4c73eee92dd652ea8e2afd7b20732cf863d4938a30f634d12c88fe64def89fd8",
    url = "https://github.com/Kotlin/dokka/releases/download/0.9.18/dokka-fatjar-0.9.18.jar",
)
