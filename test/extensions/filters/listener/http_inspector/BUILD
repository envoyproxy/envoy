load(
    "//bazel:envoy_build_system.bzl",
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
    extension_name = "envoy.filters.listener.http_inspector",
    tags = ["fails_on_windows"],
    deps = [
        "//source/common/common:hex_lib",
        "//source/common/http:utility_lib",
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
    extension_name = "envoy.filters.listener.http_inspector",
    deps = [
        "//source/extensions/filters/listener:well_known_names",
        "//source/extensions/filters/listener/http_inspector:config",
        "//source/extensions/filters/listener/http_inspector:http_inspector_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:listener_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)
