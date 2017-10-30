licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    external_deps = ["envoy_filter_accesslog"],
    deps = [
        "//source/common/config:well_known_names",
        "//source/common/http/access_log:access_log_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/server/config/http:buffer_lib",
        "//source/server/config/http:dynamo_lib",
        "//source/server/config/http:fault_lib",
        "//source/server/config/http:file_access_log_lib",
        "//source/server/config/http:grpc_http1_bridge_lib",
        "//source/server/config/http:grpc_web_lib",
        "//source/server/config/http:ip_tagging_lib",
        "//source/server/config/http:ratelimit_lib",
        "//source/server/config/http:router_lib",
        "//source/server/config/http:zipkin_lib",
        "//source/server/http:health_check_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "lightstep_config_test",
    srcs = ["lightstep_config_test.cc"],
    deps = [
        "//source/server/config/http:lightstep_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)
