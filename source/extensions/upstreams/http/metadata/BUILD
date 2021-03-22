load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "upstream_request_lib",
    srcs = ["upstream_request.cc"],
    hdrs = ["upstream_request.h"],
    category = "envoy.upstreams",
    security_posture = "unknown",
    status = "wip",
    visibility = ["//visibility:public"],
    deps = [
        "//include/envoy/http:codec_interface",
        "//include/envoy/router:router_interface",
        "//source/extensions/upstreams/http/http:upstream_request_lib",
    ],
)
