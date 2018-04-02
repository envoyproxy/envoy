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
        "extractor.cc",
    ],
    hdrs = [
        "extractor.h",
    ],
    external_deps = [
        "rapidjson",
        "ssl",
    ],
    repository = "@envoy",
    deps = [
        "//source/exe:envoy_common_lib",
        "@envoy_api//envoy/config/filter/http/jwt_authn/v2alpha:jwt_authn_cc",
    ],
)
