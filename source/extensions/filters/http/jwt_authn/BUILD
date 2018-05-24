licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "jwt_lib",
    srcs = [
        "authenticator.cc",
        "extractor.cc",
        "filter.cc",
        "filter_factory.cc",
        "jwks_cache.cc",
    ],
    hdrs = [
        "authenticator.h",
        "extractor.h",
        "filter.h",
        "filter_config.h",
        "filter_factory.h",
        "jwks_cache.h",
    ],
    external_deps = [
        "jwt_verify_lib",
        "rapidjson",
        "ssl",
    ],
    repository = "@envoy",
    deps = [
        "//source/exe:envoy_common_lib",
        "@envoy_api//envoy/config/filter/http/jwt_authn/v2alpha:jwt_authn_cc",
    ],
)
