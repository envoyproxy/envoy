load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.listener.original_src"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/listener/original_src:config_lib",
        "@envoy_api//envoy/extensions/filters/listener/original_src/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "original_src_config_factory_test",
    srcs = ["original_src_config_factory_test.cc"],
    extension_names = ["envoy.filters.listener.original_src"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/listener/original_src:config",
        "//source/extensions/filters/listener/original_src:config_lib",
        "//source/extensions/filters/listener/original_src:original_src_lib",
        "//test/mocks/server:listener_factory_context_mocks",
    ],
)

envoy_extension_cc_test(
    name = "original_src_test",
    srcs = ["original_src_test.cc"],
    extension_names = ["envoy.filters.listener.original_src"],
    rbe_pool = "2core",
    deps = [
        "//source/common/network:socket_option_lib",
        "//source/extensions/filters/listener/original_src:original_src_lib",
        "//test/mocks:common_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:printers_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/listener/original_src/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "original_src_fuzz_test_proto",
    srcs = ["original_src_fuzz_test.proto"],
    deps = [
        "//test/extensions/filters/listener/common/fuzz:listener_filter_fuzzer_proto",
        "@envoy_api//envoy/extensions/filters/listener/original_src/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "original_src_fuzz_test",
    srcs = ["original_src_fuzz_test.cc"],
    corpus = "original_src_corpus",
    rbe_pool = "2core",
    deps = [
        ":original_src_fuzz_test_proto_cc_proto",
        "//source/extensions/filters/listener/original_src:original_src_lib",
        "//test/extensions/filters/listener/common/fuzz:listener_filter_fuzzer_lib",
    ],
)
