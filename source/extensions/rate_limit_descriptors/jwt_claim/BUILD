load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/ratelimit:ratelimit_interface",
        "//envoy/registry",
        "//source/common/http:header_map_lib",
        "//source/common/http:utility_lib",
        "//source/common/jwt:jwt_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/rate_limit_descriptors/jwt_claim/v3:pkg_cc_proto",
    ],
)
