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
    name = "proxy_protocol_test",
    srcs = ["proxy_protocol_test.cc"],
    extension_name = "envoy.filters.listener.proxy_protocol",
    tags = ["fails_on_windows"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_balancer_lib",
        "//source/common/network:listener_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/listener/proxy_protocol:config",
        "//source/extensions/filters/listener/proxy_protocol:proxy_protocol_lib",
        "//source/server:connection_handler_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:listener_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "proxy_protocol_fuzz_test_proto",
    srcs = ["proxy_protocol_fuzz_test.proto"],
    deps = [
        "//test/extensions/filters/listener/common/fuzz:listener_filter_fuzzer_proto",
        "@envoy_api//envoy/extensions/filters/listener/proxy_protocol/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "proxy_protocol_fuzz_test",
    srcs = ["proxy_protocol_fuzz_test.cc"],
    corpus = "proxy_protocol_corpus",
    deps = [
        ":proxy_protocol_fuzz_test_proto_cc_proto",
        "//source/extensions/filters/listener/proxy_protocol:proxy_protocol_lib",
        "//test/extensions/filters/listener/common/fuzz:listener_filter_fuzzer_lib",
    ],
)
