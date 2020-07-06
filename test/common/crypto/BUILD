load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_package",
    "envoy_proto_library",
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

envoy_proto_library(
    name = "verify_signature_fuzz_proto",
    srcs = ["verify_signature_fuzz.proto"],
)

envoy_cc_fuzz_test(
    name = "get_sha_256_digest_fuzz_test",
    srcs = ["get_sha_256_digest_fuzz_test.cc"],
    corpus = "get_sha_256_digest_corpus",
    deps = ["//source/extensions/common/crypto:utility_lib"],
)

envoy_cc_fuzz_test(
    name = "verify_signature_fuzz_test",
    srcs = ["verify_signature_fuzz_test.cc"],
    corpus = "verify_signature_corpus",
    dictionaries = ["verify_signature_fuzz_test.dict"],
    deps = [
        ":verify_signature_fuzz_proto_cc_proto",
        "//source/common/common:hex_lib",
        "//source/common/crypto:utility_lib",
        "//source/extensions/common/crypto:utility_lib",
    ],
)
