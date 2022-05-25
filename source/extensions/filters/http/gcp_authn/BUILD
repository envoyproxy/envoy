load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "gcp_authn",
    srcs = ["gcp_authn_filter.cc"],
    hdrs = ["gcp_authn_filter.h"],
    deps = [
        "gcp_authn_lib",
        "token_cache",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "gcp_authn_lib",
    srcs = ["gcp_authn_impl.cc"],
    hdrs = ["gcp_authn_impl.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "token_cache",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "token_cache",
    hdrs = ["token_cache.h"],
    external_deps = [
        "jwt_verify_lib",
        "simple_lru_cache_lib",
    ],
    deps = [
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["filter_config.cc"],
    hdrs = ["filter_config.h"],
    deps = [
        ":gcp_authn",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
    ],
)
