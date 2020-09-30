load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "mutation",
    srcs = ["mutation.cc"],
    hdrs = ["mutation.h"],
    deps = [
        "//include/envoy/grpc:async_client_interface",
        "//include/envoy/http:filter_interface",
        "@envoy_api//envoy/extensions/filters/http/mutation/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "unknown",
    deps = [
        ":mutation",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/mutation/v3alpha:pkg_cc_proto",
    ],
)
