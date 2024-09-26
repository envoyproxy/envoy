load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test_library",
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
    extension_names = ["envoy.filters.listener.proxy_protocol"],
    rbe_pool = "2core",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/listener_manager:connection_handler_lib",
        "//source/common/network:connection_balancer_lib",
        "//source/common/network:listener_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/listener/proxy_protocol:config",
        "//source/extensions/filters/listener/proxy_protocol:proxy_protocol_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:listener_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
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
    rbe_pool = "2core",
    deps = [
        ":proxy_protocol_fuzz_test_proto_cc_proto",
        "//source/extensions/filters/listener/proxy_protocol:proxy_protocol_lib",
        "//test/extensions/filters/listener/common/fuzz:listener_filter_fuzzer_lib",
    ],
)

envoy_cc_test_library(
    name = "proxy_proto_integration_test_lib",
    hdrs = ["proxy_proto_integration_test.h"],
    deps = ["@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto"],
)

envoy_extension_cc_test(
    name = "proxy_proto_integration_test",
    size = "large",
    srcs = ["proxy_proto_integration_test.cc"],
    extension_names = ["envoy.filters.listener.proxy_protocol"],
    rbe_pool = "2core",
    deps = [
        ":proxy_proto_integration_test_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:codec_client_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/clusters/original_dst:original_dst_cluster_lib",
        "//source/extensions/filters/listener/proxy_protocol:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)
