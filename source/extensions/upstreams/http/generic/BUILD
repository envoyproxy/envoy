load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    security_posture = "robust_to_untrusted_downstream",
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/router:router_lib",
        "@envoy_api//envoy/extensions/upstreams/http/generic/v3:pkg_cc_proto",
    ],
)
