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
        "//envoy/runtime:runtime_interface",
        "//source/common/http:header_utility_lib",
        "//source/common/http:utility_lib",
        "@com_google_absl//absl/container:btree",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "stats_lib",
    hdrs = ["stats.h"],
    deps = [
        "//envoy/stats:stats_macros",
    ],
)

envoy_cc_library(
    name = "jwks_async_fetcher_lib",
    srcs = ["jwks_async_fetcher.cc"],
    hdrs = ["jwks_async_fetcher.h"],
    deps = [
        ":stats_lib",
        "//envoy/server:factory_context_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/init:target_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/filters/http/common:jwks_fetcher_lib",
        "@com_github_google_jwt_verify//:jwt_verify_lib",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "jwks_cache_lib",
    srcs = ["jwks_cache.cc"],
    hdrs = ["jwks_cache.h"],
    deps = [
        "jwks_async_fetcher_lib",
        ":jwt_cache_lib",
        "//source/common/config:datasource_lib",
        "@com_github_google_jwt_verify//:jwt_verify_lib",
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
        "//envoy/server:filter_config_interface",
        "//envoy/stats:stats_macros",
        "//source/common/http:message_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/filters/http/common:jwks_fetcher_lib",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        ":filter_config_lib",
        ":matchers_lib",
        "//envoy/http:filter_interface",
        "//source/common/http:headers_lib",
        "@com_github_google_jwt_verify//:jwt_verify_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["filter_factory.cc"],
    hdrs = ["filter_factory.h"],
    deps = [
        ":filter_lib",
        "//envoy/registry",
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
        "//envoy/http:header_map_interface",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_config_lib",
    srcs = ["filter_config.cc"],
    hdrs = ["filter_config.h"],
    deps = [
        ":jwks_cache_lib",
        ":matchers_lib",
        "//envoy/router:string_accessor_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stats:stats_macros",
        "//envoy/thread_local:thread_local_interface",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "jwt_cache_lib",
    srcs = ["jwt_cache.cc"],
    hdrs = ["jwt_cache.h"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "@com_github_google_jwt_verify//:jwt_verify_lib",
        "@com_github_google_jwt_verify//:simple_lru_cache_lib",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)
