load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "extractor_lib",
    srcs = ["extractor.cc"],
    hdrs = ["extractor.h"],
    deps = [
        "//source/common/http:header_utility_lib",
        "//source/common/http:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
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
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "authenticator_lib",
    srcs = ["authenticator.cc"],
    hdrs = ["authenticator.h"],
    deps = [
        ":extractor_lib",
        ":jwks_cache_lib",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/http:message_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/filters/http/common:jwks_fetcher_lib",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    external_deps = [
        "jwt_verify_lib",
    ],
    deps = [
        ":filter_config_interface",
        ":matchers_lib",
        "//include/envoy/http:filter_interface",
        "//source/common/http:headers_lib",
        "//source/extensions/filters/http:well_known_names",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["filter_factory.cc"],
    hdrs = ["filter_factory.h"],
    security_posture = "robust_to_untrusted_downstream",
    status = "alpha",
    deps = [
        ":filter_lib",
        "//include/envoy/registry",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "matchers_lib",
    srcs = ["matcher.cc"],
    hdrs = ["matcher.h"],
    deps = [
        ":verifier_lib",
        "//source/common/common:matchers_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/router:config_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "verifier_lib",
    srcs = ["verifier.cc"],
    hdrs = ["verifier.h"],
    deps = [
        ":authenticator_lib",
        ":extractor_lib",
        "//include/envoy/http:header_map_interface",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_config_interface",
    srcs = ["filter_config.cc"],
    hdrs = ["filter_config.h"],
    deps = [
        ":jwks_cache_lib",
        ":matchers_lib",
        "//include/envoy/router:string_accessor_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/thread_local:thread_local_interface",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)
