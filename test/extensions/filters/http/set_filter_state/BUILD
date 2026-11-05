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
    name = "integration_test",
    srcs = [
        "integration_test.cc",
    ],
    extension_names = ["envoy.filters.http.set_filter_state"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/router:string_accessor_lib",
        "//source/extensions/filters/http/set_filter_state:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "upstream_integration_test",
    size = "large",
    srcs = [
        "upstream_integration_test.cc",
    ],
    extension_names = ["envoy.filters.http.set_filter_state"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/config:metadata_lib",
        "//source/extensions/filters/http/header_mutation:config",
        "//source/extensions/filters/http/set_filter_state:config",
        "//source/extensions/retry/host/previous_hosts:config",
        "//test/integration:http_integration_lib",
        "//test/mocks/http:http_mocks",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/retry/host/previous_hosts/v3:pkg_cc_proto",
    ],
)
