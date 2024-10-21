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
    name = "http_inspector_test",
    srcs = ["http_inspector_test.cc"],
    extension_names = ["envoy.filters.listener.http_inspector"],
    rbe_pool = "2core",
    #TODO(davinci26): The test passes on Windows *but* http inspector
    # *used* to rely on Event::FileTriggerType::Edge and we got away with it
    # because we mock the dispatcher. Need to verify that the scenario is
    # actually working.
    deps = [
        "//source/common/common:hex_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/common/network:listener_filter_buffer_lib",
        "//source/extensions/filters/listener/http_inspector:http_inspector_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_extension_cc_test(
    name = "http_inspector_config_test",
    srcs = ["http_inspector_config_test.cc"],
    extension_names = ["envoy.filters.listener.http_inspector"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/listener/http_inspector:config",
        "//source/extensions/filters/listener/http_inspector:http_inspector_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:listener_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_extension_cc_test(
    name = "http_inspector_parser_callbacks_test",
    srcs = ["http_inspector_parser_callbacks_test.cc"],
    extension_names = ["envoy.filters.listener.http_inspector"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/listener/http_inspector:http_inspector_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "http_inspector_fuzz_test",
    srcs = ["http_inspector_fuzz_test.cc"],
    corpus = "http_inspector_corpus",
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/listener/http_inspector:http_inspector_lib",
        "//test/extensions/filters/listener/common/fuzz:listener_filter_fuzzer_lib",
    ],
)

envoy_extension_cc_test(
    name = "http_inspector_integration_test",
    size = "large",
    srcs = [
        "http_inspector_integration_test.cc",
    ],
    extension_names = ["envoy.filters.listener.http_inspector"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/filters/listener/http_inspector:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/integration:base_integration_test_lib",
        "//test/integration:common_extensions_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/listener/http_inspector/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)
