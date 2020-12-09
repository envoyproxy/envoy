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

def upstream_envoy_overrides():
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
            "@envoy_mobile//bazel:protobuf.patch",
        ],
        sha256 = "d7cfd31620a352b2ee8c1ed883222a0d77e44346643458e062e86b1d069ace3e",
        strip_prefix = "protobuf-3.10.1",
        urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v3.10.1/protobuf-all-3.10.1.tar.gz"],
    )

    # Workaround old NDK version breakages https://github.com/lyft/envoy-mobile/issues/934
    http_archive(
        name = "com_github_libevent_libevent",
        urls = ["https://github.com/libevent/libevent/archive/0d7d85c2083f7a4c9efe01c061486f332b576d28.tar.gz"],
        strip_prefix = "libevent-0d7d85c2083f7a4c9efe01c061486f332b576d28",
        sha256 = "549d34065eb2485dfad6c8de638caaa6616ed130eec36dd978f73b6bdd5af113",
        build_file_content = """filegroup(name = "all", srcs = glob(["**"]), visibility = ["//visibility:public"])""",
    )

    # Patch upstream Abseil to prevent Foundation dependency from leaking into Android builds.
    # Workaround for https://github.com/abseil/abseil-cpp/issues/326.
    # TODO: Should be removed in https://github.com/lyft/envoy-mobile/issues/136 once rules_android
    # supports platform toolchains.
    http_archive(
        name = "com_google_absl",
        patches = ["@envoy_mobile//bazel:abseil.patch"],
        sha256 = "635367c5cac4bbab95d0485ba9e68fa422546b06ce050190c99be7e23aba3ce3",
        strip_prefix = "abseil-cpp-8f1c34a77a2ba04512b7f9cbc6013d405e6a0b31",
        # 2020-08-08
        urls = ["https://github.com/abseil/abseil-cpp/archive/8f1c34a77a2ba04512b7f9cbc6013d405e6a0b31.tar.gz"],
    )

    # This should be kept in sync with Envoy itself, we just need to apply this patch
    # Remove this once https://boringssl-review.googlesource.com/c/boringssl/+/37804 is in master-with-bazel
    http_archive(
        name = "boringssl",
        patches = ["@envoy_mobile//bazel:boringssl.patch"],
        sha256 = "36049e6cd09b353c83878cae0dd84e8b603ba1a40dcd74e44ebad101fc5c672d",
        strip_prefix = "boringssl-37b57ed537987f1b4c60c60fa1aba20f3a0f6d26",
        urls = ["https://github.com/google/boringssl/archive/37b57ed537987f1b4c60c60fa1aba20f3a0f6d26.tar.gz"],
    )

def swift_repos():
    http_archive(
        name = "build_bazel_rules_apple",
        sha256 = "8219971a5e7bb00827239ff997a3a5bde35fc06f29595f6861fc79528f370e20",
        strip_prefix = "rules_apple-b35ed275931a316b34089fc7fa5e1f20fbf8a15d",
        url = "https://github.com/bazelbuild/rules_apple/archive/b35ed275931a316b34089fc7fa5e1f20fbf8a15d.tar.gz",
    )

    http_archive(
        name = "build_bazel_rules_swift",
        sha256 = "e571fd8920dbca579b042ce8350e13639d10d3386a7fa2518956b25c7c70e758",
        strip_prefix = "rules_swift-f868e5fcf89d1639b9c1593f920dae30e10cfc72",
        url = "https://github.com/bazelbuild/rules_swift/archive/f868e5fcf89d1639b9c1593f920dae30e10cfc72.tar.gz",
    )

def kotlin_repos():
    http_archive(
        name = "rules_jvm_external",
        sha256 = "1bbf2e48d07686707dd85357e9a94da775e1dbd7c464272b3664283c9c716d26",
        strip_prefix = "rules_jvm_external-2.10",
        url = "https://github.com/bazelbuild/rules_jvm_external/archive/2.10.zip",
    )

    http_archive(
        name = "io_bazel_rules_kotlin",
        sha256 = "dc1c76f91228ddaf4f7ca4190b82d61939e95369f61dea715e8be28792072b1b",
        strip_prefix = "rules_kotlin-legacy-1.3.0-rc2",
        type = "zip",
        urls = ["https://github.com/bazelbuild/rules_kotlin/archive/legacy-1.3.0-rc2.zip"],
    )

    http_archive(
        name = "rules_detekt",
        sha256 = "b1b4c8a3228f880a169ab60a817619bc4cf254443196e7e108ece411cb9c580e",
        strip_prefix = "bazel_rules_detekt-0.3.0",
        url = "https://github.com/buildfoundation/bazel_rules_detekt/archive/v0.3.0.tar.gz",
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

    http_archive(
        name = "rules_proto_grpc",
        sha256 = "1e08cd6c61f893417b14930ca342950f5f22f71f929a38a8c4bbfeae2a80d03e",
        strip_prefix = "rules_proto_grpc-0.2.0",
        urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/archive/0.2.0.tar.gz"],
    )

    # Dokka 0.10.0 introduced a bug which makes the CLI tool error out:
    # https://github.com/Kotlin/dokka/issues/942
    http_jar(
        name = "kotlin_dokka",
        sha256 = "4c73eee92dd652ea8e2afd7b20732cf863d4938a30f634d12c88fe64def89fd8",
        url = "https://github.com/Kotlin/dokka/releases/download/0.9.18/dokka-fatjar-0.9.18.jar",
    )

    http_file(
        name = "kotlin_formatter",
        executable = 1,
        sha256 = "115d4c5cb3421eae732c42c137f5db8881ff9cc1ef180a01e638283f3ccbae44",
        urls = ["https://github.com/pinterest/ktlint/releases/download/0.37.1/ktlint"],
    )

def android_repos():
    http_archive(
        name = "build_bazel_rules_android",
        urls = ["https://github.com/bazelbuild/rules_android/archive/v0.1.1.zip"],
        sha256 = "cd06d15dd8bb59926e4d65f9003bfc20f9da4b2519985c27e190cddc8b7a7806",
        strip_prefix = "rules_android-0.1.1",
    )
