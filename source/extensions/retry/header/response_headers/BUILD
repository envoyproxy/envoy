licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "response_headers_lib",
    srcs = ["response_headers.cc"],
    hdrs = ["response_headers.h"],
    deps = [
        "//include/envoy/upstream:retry_interface",
        "//include/envoy/router:router_interface",
        "//source/common/common:utility_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":response_headers_lib",
        "//include/envoy/registry",
        "//include/envoy/upstream:retry_interface",
        "//source/common/protobuf",
        "//source/extensions/retry/header:well_known_names",
        "@envoy_api//envoy/config/retry/response_headers/v2:pkg_cc_proto",
    ],
)
