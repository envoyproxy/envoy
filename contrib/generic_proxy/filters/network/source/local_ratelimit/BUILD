load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "local_ratelimit_lib",
    srcs = [
        "local_ratelimit.cc",
    ],
    hdrs = [
        "local_ratelimit.h",
    ],
    deps = [
        "//contrib/generic_proxy/filters/network/source:match_lib",
        "//contrib/generic_proxy/filters/network/source/interface:filter_interface",
        "//contrib/generic_proxy/filters/network/source/interface:stream_interface",
        "//envoy/stats:stats_macros",
        "//source/extensions/filters/common/local_ratelimit:local_ratelimit_lib",
        "//source/extensions/filters/common/ratelimit:ratelimit_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/generic_proxy/local_ratelimit/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/common/ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    deps = [
        ":local_ratelimit_lib",
        "//contrib/generic_proxy/filters/network/source/interface:config_interface",
        "//envoy/registry",
        "@envoy_api//contrib/envoy/extensions/filters/network/generic_proxy/local_ratelimit/v3:pkg_cc_proto",
    ],
)
