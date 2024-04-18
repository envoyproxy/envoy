load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "cel_input_lib",
    srcs = ["cel_input.cc"],
    hdrs = ["cel_input.h"],
    extra_visibility = [
    ],
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//source/common/http:header_utility_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)
