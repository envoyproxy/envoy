load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "default_early_data_option_lib",
    srcs = ["default_early_data_option.cc"],
    hdrs = ["default_early_data_option.h"],
    visibility = [
        "//source/common/router:__subpackages__",
    ],
    deps = [
        "//envoy/router:router_interface",
        "//source/common/http:utility_lib",
        "@envoy_api//envoy/extensions/early_data_option/v3:pkg_cc_proto",
    ],
)
