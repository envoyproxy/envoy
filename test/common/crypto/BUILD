load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "utility_test",
    srcs = [
        "utility_test.cc",
    ],
    external_deps = [
        "ssl",
    ],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:hex_lib",
        "//source/common/crypto:utility_lib",
        "//source/extensions/common/crypto:utility_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "import_public_key_fuzz_test",
    srcs = ["import_public_key_fuzz_test.cc"],
    corpus = "import_public_key_corpus",
    deps = ["//source/extensions/common/crypto:utility_lib"],
)

envoy_cc_fuzz_test(
    name = "get_sha_256_digest_fuzz_test",
    srcs = ["get_sha_256_digest_fuzz_test.cc"],
    corpus = "get_sha_256_digest_corpus",
    deps = ["//source/extensions/common/crypto:utility_lib"],
)
