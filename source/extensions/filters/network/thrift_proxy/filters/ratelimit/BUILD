load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "ratelimit_lib",
    srcs = ["ratelimit.cc"],
    hdrs = ["ratelimit.h"],
    deps = [
        "//envoy/stats:stats_macros",
        "//source/common/protobuf:utility_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/filters/common/ratelimit:ratelimit_client_interface",
        "//source/extensions/filters/common/ratelimit:ratelimit_lib",
        "//source/extensions/filters/common/ratelimit:stat_names_lib",
        "//source/extensions/filters/network/thrift_proxy:app_exception_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:pass_through_filter_lib",
        "//source/extensions/filters/network/thrift_proxy/router:router_ratelimit_interface",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/filters/ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":ratelimit_lib",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/ratelimit:ratelimit_client_interface",
        "//source/extensions/filters/common/ratelimit:ratelimit_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/filters/ratelimit/v3:pkg_cc_proto",
    ],
)
