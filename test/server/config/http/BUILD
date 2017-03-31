package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_test")

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//source/server/config/http:buffer_lib",
        "//source/server/config/http:dynamo_lib",
        "//source/server/config/http:fault_lib",
        "//source/server/config/http:grpc_http1_bridge_lib",
        "//source/server/config/http:ratelimit_lib",
        "//source/server/config/http:router_lib",
        "//source/server/http:health_check_lib",
        "//test/mocks/server:server_mocks",
    ],
)
