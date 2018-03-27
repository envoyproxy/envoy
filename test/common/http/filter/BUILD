licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "buffer_filter_test",
    srcs = ["buffer_filter_test.cc"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//source/common/http:header_map_lib",
        "//source/common/http/filter:buffer_filter_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "cors_filter_test",
    srcs = ["cors_filter_test.cc"],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/http/filter:cors_filter_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "fault_filter_test",
    srcs = ["fault_filter_test.cc"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/config:filter_json_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http/filter:fault_filter_lib",
        "//source/common/stats:stats_lib",
        "//test/common/http:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "gzip_filter_test",
    srcs = ["gzip_filter_test.cc"],
    deps = [
        "//source/common/compressor:compressor_lib",
        "//source/common/decompressor:decompressor_lib",
        "//source/common/http/filter:gzip_filter_lib",
        "//source/common/protobuf:utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "ip_tagging_filter_test",
    srcs = ["ip_tagging_filter_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/config:filter_json_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http/filter:ip_tagging_filter_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "ratelimit_test",
    srcs = ["ratelimit_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/config:filter_json_lib",
        "//source/common/http:headers_lib",
        "//source/common/http/filter:ratelimit_includes",
        "//source/common/http/filter:ratelimit_lib",
        "//source/common/ratelimit:ratelimit_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/ratelimit:ratelimit_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/filter/http/ext_authz/v2:ext_authz_cc",
    ],
)

envoy_cc_test(
    name = "squash_filter_test",
    srcs = ["squash_filter_test.cc"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//source/common/http:header_map_lib",
        "//source/common/http/filter:squash_filter_lib",
        "//source/common/stats:stats_lib",
        "//source/server/config/http:squash_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)
