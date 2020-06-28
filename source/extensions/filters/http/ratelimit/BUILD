load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# Ratelimit L7 HTTP filter
# Public docs: docs/root/configuration/http_filters/rate_limit_filter.rst

envoy_package()

envoy_cc_library(
    name = "ratelimit_lib",
    srcs = ["ratelimit.cc"],
    hdrs = ["ratelimit.h"],
    deps = [
        "//include/envoy/http:codes_interface",
        "//include/envoy/ratelimit:ratelimit_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/http:codes_lib",
        "//source/common/router:config_lib",
        "//source/extensions/filters/common/ratelimit:ratelimit_client_interface",
        "//source/extensions/filters/common/ratelimit:stat_names_lib",
        "@envoy_api//envoy/extensions/filters/http/ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":ratelimit_lib",
        "//include/envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/ratelimit:ratelimit_client_interface",
        "//source/extensions/filters/common/ratelimit:ratelimit_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/ratelimit/v3:pkg_cc_proto",
    ],
)
