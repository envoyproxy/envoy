load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
    "envoy_select_enable_http3",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//source/extensions/upstreams/http/http:upstream_request_lib",
        "//source/extensions/upstreams/http/tcp:upstream_request_lib",
        "@envoy_api//envoy/extensions/upstreams/http/generic/v3:pkg_cc_proto",
    ] + envoy_select_enable_http3(["//source/extensions/upstreams/http/udp:upstream_request_lib"]),
)
