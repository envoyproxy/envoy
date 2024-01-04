load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive", "http_file")

def envoy_mobile_repositories():
    http_archive(
        name = "google_bazel_common",
        sha256 = "0ba40405bc4cc095dd1ace08d145fe238798388f26c4ad0725e801b7e16e0f27",
        strip_prefix = "bazel-common-d4ada735afa0ab044957cfa21849be577756a6cd",
        urls = ["https://github.com/google/bazel-common/archive/d4ada735afa0ab044957cfa21849be577756a6cd.zip"],
    )

    upstream_envoy_overrides()
    swift_repos()
    kotlin_repos()
    android_repos()
    python_repos()

def upstream_envoy_overrides():
    # Workaround old NDK version breakages https://github.com/envoyproxy/envoy-mobile/issues/934
    http_archive(
        name = "com_github_libevent_libevent",
        urls = ["https://github.com/libevent/libevent/archive/0c54433c120309ff14c90de40567b214d5335306.tar.gz"],
        strip_prefix = "libevent-0c54433c120309ff14c90de40567b214d5335306",
        sha256 = "7cd416026a88498ecac15a51a4839bb4ec43d76f63d31feca9160d38aff39bc4",
        build_file_content = """filegroup(name = "all", srcs = glob(["**"]), visibility = ["//visibility:public"])""",
    )

def swift_repos():
    http_archive(
        name = "DrString",
        build_file_content = """exports_files(["drstring"])""",
        sha256 = "860788450cf9900613454a51276366ea324d5bfe71d1844106e9c1f1d7dfd82b",
        url = "https://github.com/dduan/DrString/releases/download/0.5.2/drstring-x86_64-apple-darwin.tar.gz",
    )

    http_archive(
        name = "SwiftLint",
        build_file_content = """exports_files(["swiftlint"])""",
        sha256 = "47078845857fa7cf8497f5861967c7ce67f91915e073fb3d3114b8b2486a9270",
        url = "https://github.com/realm/SwiftLint/releases/download/0.50.3/portable_swiftlint.zip",
    )

    http_archive(
        name = "com_github_buildbuddy_io_rules_xcodeproj",
        sha256 = "d02932255ba3ffaab1859e44528c69988e93fa353fa349243e1ef5054bd1ba80",
        url = "https://github.com/buildbuddy-io/rules_xcodeproj/releases/download/1.2.0/release.tar.gz",
    )

def kotlin_repos():
    http_archive(
        name = "rules_java",
        sha256 = "241822bf5fad614e3e1c42431002abd9af757136fa590a6a7870c6e0640a82e3",
        strip_prefix = "rules_java-6.4.0",
        url = "https://github.com/bazelbuild/rules_java/archive/6.4.0.tar.gz",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:rules_java.patch"],
    )

    http_archive(
        name = "rules_jvm_external",
        sha256 = "b17d7388feb9bfa7f2fa09031b32707df529f26c91ab9e5d909eb1676badd9a6",
        strip_prefix = "rules_jvm_external-4.5",
        url = "https://github.com/bazelbuild/rules_jvm_external/archive/4.5.zip",
    )

    http_archive(
        name = "io_bazel_rules_kotlin",
        sha256 = "01293740a16e474669aba5b5a1fe3d368de5832442f164e4fbfc566815a8bc3a",
        urls = ["https://github.com/bazelbuild/rules_kotlin/releases/download/v1.8/rules_kotlin_release.tgz"],
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
        strip_prefix = "pybind11_bazel-23926b00e2b2eb2fc46b17e587cf0c0cfd2f2c4b",
        urls = ["https://github.com/pybind/pybind11_bazel/archive/23926b00e2b2eb2fc46b17e587cf0c0cfd2f2c4b.zip"],
        sha256 = "07e529a85cf4c11e1ca1b423149e86e63a3f3859c22efee3b3c5225ca89580f2",
    )
    http_archive(
        name = "pybind11",
        build_file = "@pybind11_bazel//:pybind11.BUILD",
        strip_prefix = "pybind11-2.6.1",
        urls = ["https://github.com/pybind/pybind11/archive/v2.6.1.tar.gz"],
        sha256 = "cdbe326d357f18b83d10322ba202d69f11b2f49e2d87ade0dc2be0c5c34f8e2a",
    )
