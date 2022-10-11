load("@bazel_gazelle//:deps.bzl", "go_repository")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive", "http_file", "http_jar")

def envoy_mobile_repositories():
    http_archive(
        name = "google_bazel_common",
        sha256 = "d8c9586b24ce4a5513d972668f94b62eb7d705b92405d4bc102131f294751f1d",
        strip_prefix = "bazel-common-413b433b91f26dbe39cdbc20f742ad6555dd1e27",
        urls = ["https://github.com/google/bazel-common/archive/413b433b91f26dbe39cdbc20f742ad6555dd1e27.zip"],
    )

    upstream_envoy_overrides()
    swift_repos()
    kotlin_repos()
    android_repos()
    python_repos()

def upstream_envoy_overrides():
    # Workaround due to a Detekt version compatibility with protobuf: https://github.com/envoyproxy/envoy-mobile/issues/1869
    http_archive(
        name = "com_google_protobuf",
        patch_args = ["-p1"],
        patches = [
            "@envoy_mobile//bazel:protobuf.patch",
        ],
        sha256 = "d7371dc2d46fddac1af8cb27c0394554b068768fc79ecaf5be1a1863e8ff3392",
        strip_prefix = "protobuf-3.16.0",
        urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v3.16.0/protobuf-all-3.16.0.tar.gz"],
    )

    # Workaround old NDK version breakages https://github.com/envoyproxy/envoy-mobile/issues/934
    http_archive(
        name = "com_github_libevent_libevent",
        urls = ["https://github.com/libevent/libevent/archive/0d7d85c2083f7a4c9efe01c061486f332b576d28.tar.gz"],
        strip_prefix = "libevent-0d7d85c2083f7a4c9efe01c061486f332b576d28",
        sha256 = "549d34065eb2485dfad6c8de638caaa6616ed130eec36dd978f73b6bdd5af113",
        build_file_content = """filegroup(name = "all", srcs = glob(["**"]), visibility = ["//visibility:public"])""",
    )

    # Patch upstream Abseil to prevent Foundation dependency from leaking into Android builds.
    # Workaround for https://github.com/abseil/abseil-cpp/issues/326.
    # TODO: Should be removed in https://github.com/envoyproxy/envoy-mobile/issues/136 once rules_android
    # supports platform toolchains.
    http_archive(
        name = "com_google_absl",
        patches = ["@envoy_mobile//bazel:abseil.patch"],
        sha256 = "3a0bb3d2e6f53352526a8d1a7e7b5749c68cd07f2401766a404fb00d2853fa49",
        strip_prefix = "abseil-cpp-4bbdb026899fea9f882a95cbd7d6a4adaf49b2dd",
        # 2022-07-05
        urls = ["https://github.com/abseil/abseil-cpp/archive/4bbdb026899fea9f882a95cbd7d6a4adaf49b2dd.tar.gz"],
    )

    # This should be kept in sync with Envoy itself, we just need to apply this patch
    # Remove this once https://boringssl-review.googlesource.com/c/boringssl/+/37804 is in master-with-bazel
    http_archive(
        name = "boringssl",
        patches = ["@envoy_mobile//bazel:boringssl.patch"],
        sha256 = "579cb415458e9f3642da0a39a72f79fdfe6dc9c1713b3a823f1e276681b9703e",
        strip_prefix = "boringssl-648cbaf033401b7fe7acdce02f275b06a88aab5c",
        urls = ["https://github.com/google/boringssl/archive/648cbaf033401b7fe7acdce02f275b06a88aab5c.tar.gz"],
    )

def swift_repos():
    http_archive(
        name = "build_bazel_rules_apple",
        sha256 = "687644bf48ccf91286f31c4ec26cf6591800b39bee8a630438626fc9bb4042de",
        strip_prefix = "rules_apple-a0f8748ce89698a599149d984999eaefd834c004",
        url = "https://github.com/bazelbuild/rules_apple/archive/a0f8748ce89698a599149d984999eaefd834c004.tar.gz",
    )

    http_archive(
        name = "build_bazel_rules_swift",
        sha256 = "51efdaf85e04e51174de76ef563f255451d5a5cd24c61ad902feeadafc7046d9",
        url = "https://github.com/bazelbuild/rules_swift/releases/download/1.2.0/rules_swift.1.2.0.tar.gz",
    )

    http_archive(
        name = "DrString",
        build_file_content = """exports_files(["drstring"])""",
        sha256 = "860788450cf9900613454a51276366ea324d5bfe71d1844106e9c1f1d7dfd82b",
        url = "https://github.com/dduan/DrString/releases/download/0.5.2/drstring-x86_64-apple-darwin.tar.gz",
    )

    http_archive(
        name = "SwiftLint",
        build_file_content = """exports_files(["swiftlint"])""",
        sha256 = "61d335766a39ba8fa499017a560950bd9fa0b0e5bc318559a9c1c7f4da679256",
        url = "https://github.com/realm/SwiftLint/releases/download/0.47.1/portable_swiftlint.zip",
    )

    http_archive(
        name = "com_github_buildbuddy_io_rules_xcodeproj",
        sha256 = "564381b33261ba29e3c8f505de82fc398452700b605d785ce3e4b9dd6c73b623",
        url = "https://github.com/buildbuddy-io/rules_xcodeproj/releases/download/0.9.0/release.tar.gz",
    )

def kotlin_repos():
    http_archive(
        name = "rules_java",
        sha256 = "19462d64b1586c0d4ea0e87f9325be2514f0eb84e56dbf3245450451b3701581",
        strip_prefix = "rules_java-43243982abc76390ef64be62379a1353f9011771",
        # TODO(jpsim): Switch back to bazelbuild repo when https://github.com/bazelbuild/rules_java/issues/64 is fixed
        url = "https://github.com/jpsim/rules_java/archive/43243982abc76390ef64be62379a1353f9011771.tar.gz",
    )

    http_archive(
        name = "rules_jvm_external",
        sha256 = "cd1a77b7b02e8e008439ca76fd34f5b07aecb8c752961f9640dea15e9e5ba1ca",
        strip_prefix = "rules_jvm_external-4.2",
        url = "https://github.com/bazelbuild/rules_jvm_external/archive/4.2.zip",
    )

    http_archive(
        name = "io_bazel_rules_kotlin",
        sha256 = "f033fa36f51073eae224f18428d9493966e67c27387728b6be2ebbdae43f140e",
        urls = ["https://github.com/bazelbuild/rules_kotlin/releases/download/v1.7.0-RC-3/rules_kotlin_release.tgz"],
    )

    http_archive(
        name = "rules_detekt",
        sha256 = "44912c74dc2e164227b1102ef36227d0e78fdbd7c7359868ae13424eb4f0d5c2",
        strip_prefix = "bazel_rules_detekt-0.6.0",
        url = "https://github.com/buildfoundation/bazel_rules_detekt/archive/v0.6.0.tar.gz",
    )

    http_archive(
        name = "rules_proto_grpc",
        sha256 = "507e38c8d95c7efa4f3b1c0595a8e8f139c885cb41a76cab7e20e4e67ae87731",
        strip_prefix = "rules_proto_grpc-4.1.1",
        urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/archive/4.1.1.tar.gz"],
    )

    http_file(
        name = "kotlin_formatter",
        executable = 1,
        sha256 = "115d4c5cb3421eae732c42c137f5db8881ff9cc1ef180a01e638283f3ccbae44",
        urls = ["https://github.com/pinterest/ktlint/releases/download/0.37.1/ktlint"],
    )

    http_archive(
        name = "robolectric",
        sha256 = "5bcde5db598f6938c9887a140a0a1249f95d3c16274d40869503d0c322a20d5d",
        urls = ["https://github.com/robolectric/robolectric-bazel/archive/4.8.2.tar.gz"],
        strip_prefix = "robolectric-bazel-4.8.2",
    )

def android_repos():
    http_archive(
        name = "build_bazel_rules_android",
        urls = ["https://github.com/bazelbuild/rules_android/archive/refs/tags/v0.1.1.zip"],
        sha256 = "cd06d15dd8bb59926e4d65f9003bfc20f9da4b2519985c27e190cddc8b7a7806",
        strip_prefix = "rules_android-0.1.1",
    )

def python_repos():
    http_archive(
        name = "pybind11_bazel",
        strip_prefix = "pybind11_bazel-26973c0ff320cb4b39e45bc3e4297b82bc3a6c09",
        urls = ["https://github.com/pybind/pybind11_bazel/archive/26973c0ff320cb4b39e45bc3e4297b82bc3a6c09.zip"],
        sha256 = "a5666d950c3344a8b0d3892a88dc6b55c8e0c78764f9294e806d69213c03f19d",
    )
    http_archive(
        name = "pybind11",
        build_file = "@pybind11_bazel//:pybind11.BUILD",
        strip_prefix = "pybind11-2.6.1",
        urls = ["https://github.com/pybind/pybind11/archive/v2.6.1.tar.gz"],
        sha256 = "cdbe326d357f18b83d10322ba202d69f11b2f49e2d87ade0dc2be0c5c34f8e2a",
    )
