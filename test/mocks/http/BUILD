package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_mock")

envoy_cc_mock(
    name = "http_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/buffer:buffer_includes",
        "//include/envoy/event:dispatcher_includes",
        "//include/envoy/http:access_log_includes",
        "//include/envoy/http:async_client_includes",
        "//include/envoy/http:codec_includes",
        "//include/envoy/http:conn_pool_includes",
        "//include/envoy/http:filter_includes",
        "//include/envoy/ssl:connection_includes",
        "//source/common/http:conn_manager_lib",
        "//source/common/http:header_map_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/upstream:host_mocks",
    ],
)
