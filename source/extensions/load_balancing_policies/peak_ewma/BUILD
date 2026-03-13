load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "peak_ewma_support_lib",
    srcs = [
        "cost.cc",
        "host_data.cc",
    ],
    hdrs = [
        "cost.h",
        "host_data.h",
    ],
    visibility = [
        "//contrib/peak_ewma/filters/http/source:__pkg__",
        "//contrib/peak_ewma/filters/http/test:__pkg__",
        "//test:__subpackages__",
    ],
    deps = [
        "//source/common/upstream:upstream_lib",
    ],
)

envoy_cc_library(
    name = "peak_ewma_lb_lib",
    srcs = ["peak_ewma_lb.cc"],
    hdrs = [
        "peak_ewma_lb.h",
    ],
    visibility = [
        "//contrib/peak_ewma/filters/http/source:__pkg__",
        "//contrib/peak_ewma/filters/http/test:__pkg__",
        "//test:__subpackages__",
    ],
    deps = [
        ":peak_ewma_support_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/load_balancing_policies/common:load_balancer_lib",
        "@envoy_api//envoy/extensions/load_balancing_policies/peak_ewma/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    extra_visibility = [
        "//contrib/peak_ewma/filters/http/test:__pkg__",
        "//test:__subpackages__",
    ],
    deps = [
        ":peak_ewma_lb_lib",
        "//envoy/registry",
        "//source/extensions/load_balancing_policies/common:factory_base",
        "@envoy_api//envoy/extensions/load_balancing_policies/peak_ewma/v3:pkg_cc_proto",
    ],
)
