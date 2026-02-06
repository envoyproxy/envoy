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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.sse_to_metadata"],
    deps = [
        "//source/extensions/filters/http/sse_to_metadata:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/content_parsers/json/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_names = ["envoy.filters.http.sse_to_metadata"],
    deps = [
        "//source/common/config:metadata_lib",
        "//source/extensions/filters/http/sse_to_metadata:config",
        "//source/extensions/filters/http/sse_to_metadata:sse_to_metadata_lib",
        "//test/common/stream_info:test_util",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/content_parsers/json/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.filters.http.sse_to_metadata"],
    deps = [
        "//source/extensions/filters/http/sse_to_metadata:config",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/sse_to_metadata/v3:pkg_cc_proto",
    ],
)
