load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "health_checker_base_lib",
    srcs = ["health_checker_base_impl.cc"],
    hdrs = ["health_checker_base_impl.h"],
    deps = [
        "//envoy/upstream:health_checker_interface",
        "//source/common/router:router_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher:pkg_cc_proto",
    ],
)
