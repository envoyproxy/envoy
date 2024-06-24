load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "inputs_lib",
    srcs = ["inputs.cc"],
    hdrs = ["inputs.h"],
    extra_visibility = [
        "//source/common/listener_manager:__subpackages__",
        "//source/common/router:__subpackages__",
        "//test:__subpackages__",
    ],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/matcher:matcher_interface",
        "//envoy/network:filter_interface",
        "//envoy/registry",
        "//source/common/network:utility_lib",
        "@envoy_api//envoy/extensions/matching/common_inputs/network/v3:pkg_cc_proto",
    ],
)
