licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "extractor_lib",
    srcs = ["extractor.cc"],
    hdrs = ["extractor.h"],
    deps = [
        "//source/common/http:utility_lib",
        "@envoy_api//envoy/config/filter/http/jwt_authn/v2alpha:jwt_authn_cc",
    ],
)

envoy_cc_library(
    name = "jwks_cache_lib",
    srcs = ["jwks_cache.cc"],
    hdrs = ["jwks_cache.h"],
    external_deps = [
        "jwt_verify_lib",
    ],
    deps = [
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:datasource_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/filter/http/jwt_authn/v2alpha:jwt_authn_cc",
    ],
)

envoy_cc_library(
    name = "authenticator_lib",
    srcs = ["authenticator.cc"],
    hdrs = [
        "authenticator.h",
        "filter_config.h",
    ],
    deps = [
        ":extractor_lib",
        ":jwks_cache_lib",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/http:message_lib",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        ":authenticator_lib",
        "//include/envoy/http:filter_interface",
        "//source/extensions/filters/http:well_known_names",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["filter_factory.cc"],
    hdrs = ["filter_factory.h"],
    deps = [
        ":filter_lib",
        "//include/envoy/registry",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
    ],
)
