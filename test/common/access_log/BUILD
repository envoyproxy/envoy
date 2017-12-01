licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "access_log_formatter_test",
    srcs = ["access_log_formatter_test.cc"],
    deps = [
        "//source/common/access_log:access_log_formatter_lib",
        "//source/common/common:utility_lib",
        "//source/common/http:header_map_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "access_log_impl_test",
    srcs = ["access_log_impl_test.cc"],
    external_deps = ["envoy_filter_network_http_connection_manager"],
    deps = [
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/runtime:uuid_util_lib",
        "//source/common/stats:stats_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/server/config/network:file_access_log_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "access_log_manager_impl_test",
    srcs = ["access_log_manager_impl_test.cc"],
    deps = [
        "//source/common/access_log:access_log_manager_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/api:api_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
    ],
)

envoy_cc_test(
    name = "request_info_impl_test",
    srcs = ["request_info_impl_test.cc"],
    deps = [
        "//include/envoy/http:protocol_interface",
        "//include/envoy/upstream:host_description_interface",
        "//source/common/access_log:request_info_lib",
        "//test/mocks/upstream:upstream_mocks",
    ],
)
