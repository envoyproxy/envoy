load("@rules_proto//proto:defs.bzl", "proto_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

proto_library(
    name = "protos",
    deps = [
        "//source/extensions/filters/http/cache/simple_http_cache:config",
    ],
)
