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
    name = "local_ratelimit_test",
    srcs = ["local_ratelimit_test.cc"],
    extension_name = "envoy.filters.network.local_ratelimit",
    deps = [
        "//source/extensions/filters/network/local_ratelimit:local_ratelimit_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "@envoy_api//envoy/extensions/filters/network/local_ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "local_ratelimit_integration_test",
    srcs = ["local_ratelimit_integration_test.cc"],
    extension_name = "envoy.filters.network.local_ratelimit",
    tags = ["fails_on_windows"],
    deps = [
        "//source/extensions/filters/network/local_ratelimit:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
    ],
)

envoy_proto_library(
    name = "local_ratelimit_fuzz_proto",
    srcs = ["local_ratelimit_fuzz.proto"],
    deps = [
        "@envoy_api//envoy/extensions/filters/network/local_ratelimit/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "local_ratelimit_fuzz_test",
    srcs = ["local_ratelimit_fuzz_test.cc"],
    corpus = "local_ratelimit_corpus",
    deps = [
        ":local_ratelimit_fuzz_proto_cc_proto",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/local_ratelimit:local_ratelimit_lib",
        "//test/fuzz:utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "@envoy_api//envoy/extensions/filters/network/local_ratelimit/v3:pkg_cc_proto",
    ],
)
