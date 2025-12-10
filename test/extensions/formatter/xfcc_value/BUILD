load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "xfcc_value_test",
    srcs = ["xfcc_value_test.cc"],
    extension_names = ["envoy.built_in_formatters.xfcc_value"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/formatter:substitution_formatter_lib",
        "//source/extensions/formatter/xfcc_value:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_fuzz_test(
    name = "xfcc_value_fuzz_test",
    srcs = ["xfcc_value_fuzz_test.cc"],
    corpus = "xfcc_value_corpus",
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/formatter/xfcc_value:config",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)
