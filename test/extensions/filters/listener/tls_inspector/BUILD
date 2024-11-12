load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_benchmark_test",
    "envoy_extension_cc_benchmark_binary",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "tls_inspector_test",
    srcs = ["tls_inspector_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":tls_utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/common/network:listener_filter_buffer_lib",
        "//source/extensions/filters/listener/tls_inspector:config",
        "//source/extensions/filters/listener/tls_inspector:tls_inspector_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_proto_library(
    name = "tls_inspector_fuzz_test_proto",
    srcs = ["tls_inspector_fuzz_test.proto"],
    deps = [
        "//test/extensions/filters/listener/common/fuzz:listener_filter_fuzzer_proto",
        "@envoy_api//envoy/extensions/filters/listener/tls_inspector/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "tls_inspector_fuzz_test",
    srcs = ["tls_inspector_fuzz_test.cc"],
    corpus = "tls_inspector_corpus",
    rbe_pool = "2core",
    deps = [
        ":tls_inspector_fuzz_test_proto_cc_proto",
        "//source/extensions/filters/listener/tls_inspector:tls_inspector_lib",
        "//test/extensions/filters/listener/common/fuzz:listener_filter_fuzzer_lib",
    ],
)

envoy_extension_cc_benchmark_binary(
    name = "tls_inspector_benchmark",
    srcs = ["tls_inspector_benchmark.cc"],
    extension_names = ["envoy.filters.listener.tls_inspector"],
    rbe_pool = "2core",
    deps = [
        ":tls_utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:listener_filter_buffer_lib",
        "//source/extensions/filters/listener/tls_inspector:tls_inspector_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "@com_github_google_benchmark//:benchmark",
    ],
)

envoy_extension_benchmark_test(
    name = "tls_inspector_benchmark_test",
    benchmark_binary = "tls_inspector_benchmark",
    extension_names = ["envoy.filters.listener.tls_inspector"],
)

envoy_cc_library(
    name = "tls_utility_lib",
    srcs = ["tls_utility.cc"],
    hdrs = ["tls_utility.h"],
    external_deps = ["ssl"],
    deps = [
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_test(
    name = "tls_inspector_integration_test",
    size = "large",
    srcs = [
        "tls_inspector_integration_test.cc",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    deps = [
        "//source/common/config:api_version_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/tls:context_config_lib",
        "//source/common/tls:context_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/filters/listener/tls_inspector:config",
        "//source/extensions/filters/listener/tls_inspector:tls_inspector_lib",
        "//source/extensions/filters/network/echo:config",
        "//source/extensions/transport_sockets/tls:config",
        "//test/integration:integration_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
    ],
)
