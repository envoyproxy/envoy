load("@rules_proto//proto:defs.bzl", "proto_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

proto_library(
    name = "protos",
    visibility = ["//visibility:public"],
    deps = [
        "//source/extensions/common/wasm/ext:declare_property_proto",
        "//source/extensions/common/wasm/ext:node_subset_proto",
        "//source/extensions/filters/http/aws_lambda:request_response",
        "//source/extensions/filters/http/cache:key",
        "//source/extensions/filters/http/cache/simple_http_cache:config",
        "//source/extensions/filters/http/oauth2:oauth_response",
        "//source/extensions/tracers/xray:daemon",
        "//source/server:hot_restart",
    ],
)
