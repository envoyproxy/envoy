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
    deps = [
        "//source/common/config:well_known_names",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:router_lib",
        "//source/extensions/filters/http/buffer:config",
        "//source/extensions/filters/http/lua:config",
        "//source/extensions/filters/http/ratelimit:config",
        "//source/server/config/http:fault_lib",
        "//source/server/config/http:grpc_http1_bridge_lib",
        "//source/server/config/http:grpc_json_transcoder_lib",
        "//source/server/config/http:grpc_web_lib",
        "//source/server/config/http:ip_tagging_lib",
        "//source/server/config/http:router_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)
