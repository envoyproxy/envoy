licenses(["notice"])  # Apache 2

# Local Ratelimit L7 HTTP filter
# Public docs: docs/root/configuration/http_filters/local_rate_limit_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "local_ratelimit_lib",
    srcs = ["local_ratelimit.cc"],
    hdrs = ["local_ratelimit.h"],
    deps = [
        "//include/envoy/http:codes_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/http:codes_lib",
        "//source/common/router:config_lib",
        "//source/extensions/filters/common/ratelimit:stat_names_lib",
        "@envoy_api//envoy/extensions/filters/http/local_ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "unknown",
    deps = [
        ":local_ratelimit_lib",
        "//include/envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/local_ratelimit/v3:pkg_cc_proto",
    ],
)
