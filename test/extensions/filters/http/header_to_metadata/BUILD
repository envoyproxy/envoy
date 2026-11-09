load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_benchmark_test",
    "envoy_extension_cc_benchmark_binary",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "header_to_metadata_filter_test",
    srcs = ["header_to_metadata_filter_test.cc"],
    extension_names = ["envoy.filters.http.header_to_metadata"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/common:base64_lib",
        "//source/extensions/filters/http/header_to_metadata:header_to_metadata_filter_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:status_utility_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/filters/http/header_to_metadata/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.header_to_metadata"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/filters/http/header_to_metadata:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/header_to_metadata/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "header_to_metadata_integration_test",
    size = "large",
    srcs = ["header_to_metadata_integration_test.cc"],
    extension_names = ["envoy.filters.http.header_to_metadata"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/filters/http/header_to_metadata:config",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/filters/http/header_to_metadata/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_benchmark_binary(
    name = "header_to_metadata_speed_test",
    srcs = ["header_to_metadata_speed_test.cc"],
    extension_names = ["envoy.filters.http.header_to_metadata"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/common:regex_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/http/header_to_metadata:header_to_metadata_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings",
        "@benchmark",
        "@envoy_api//envoy/extensions/filters/http/header_to_metadata/v3:pkg_cc_proto",
    ],
)

envoy_extension_benchmark_test(
    name = "header_to_metadata_speed_test_benchmark_test",
    benchmark_binary = "header_to_metadata_speed_test",
    extension_names = ["envoy.filters.http.header_to_metadata"],
    rbe_pool = "linux_x64_small",
)
