load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive", "http_file")

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

def upstream_envoy_overrides():
    # Workaround old NDK version breakages https://github.com/envoyproxy/envoy-mobile/issues/934
    http_archive(
        name = "com_github_libevent_libevent",
        urls = ["https://github.com/libevent/libevent/archive/0d7d85c2083f7a4c9efe01c061486f332b576d28.tar.gz"],
        strip_prefix = "libevent-0d7d85c2083f7a4c9efe01c061486f332b576d28",
        sha256 = "549d34065eb2485dfad6c8de638caaa6616ed130eec36dd978f73b6bdd5af113",
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
        sha256 = "c0ee60f8757f140c157fc2c7af703d819514de6e025ebf70386d38bdd85fce83",
        url = "https://github.com/bazelbuild/rules_java/releases/download/7.12.3/rules_java-7.12.3.tar.gz",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:rules_java.patch"],
    )

    http_archive(
        name = "rules_jvm_external",
        sha256 = "3afe5195069bd379373528899c03a3072f568d33bd96fe037bd43b1f590535e7",
        strip_prefix = "rules_jvm_external-6.6",
        url = "https://github.com/bazelbuild/rules_jvm_external/releases/download/6.6/rules_jvm_external-6.6.tar.gz",
    )

    http_archive(
        name = "rules_kotlin",
        sha256 = "3b772976fec7bdcda1d84b9d39b176589424c047eb2175bed09aac630e50af43",
        urls = ["https://github.com/bazelbuild/rules_kotlin/releases/download/v1.9.6/rules_kotlin-v1.9.6.tar.gz"],
    )

    http_archive(
        name = "rules_detekt",
        sha256 = "91837e301379c105ff4565ca822f6a6b30531f0b2ab6e75bbaf74e64f7d6879c",
        strip_prefix = "bazel_rules_detekt-0.8.1.2",
        url = "https://github.com/buildfoundation/bazel_rules_detekt/archive/v0.8.1.2.tar.gz",
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
        name = "rules_android",
        urls = ["https://github.com/bazelbuild/rules_android/archive/refs/tags/v0.1.1.zip"],
        sha256 = "cd06d15dd8bb59926e4d65f9003bfc20f9da4b2519985c27e190cddc8b7a7806",
        strip_prefix = "rules_android-0.1.1",
    )
    http_archive(
        name = "rules_android_ndk",
        urls = ["https://github.com/bazelbuild/rules_android_ndk/archive/v0.1.2.tar.gz"],
        sha256 = "65aedff0cd728bee394f6fb8e65ba39c4c5efb11b29b766356922d4a74c623f5",
        strip_prefix = "rules_android_ndk-0.1.2",
        patch_args = ["-p1"],
        patches = ["//bazel:rules_android_ndk.patch"],
    )
