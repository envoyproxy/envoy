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
        "jwks.cc",
        "jwt.cc",
        "status.cc",
        "verifier.cc",
    ],
    hdrs = [
        "jwks.h",
        "jwt.h",
        "status.h",
        "utils.h",
        "verifier.h",
    ],
    external_deps = [
        "rapidjson",
        "ssl",
    ],
    repository = "@envoy",
    deps = [
        "//source/exe:envoy_common_lib",
    ],
)
