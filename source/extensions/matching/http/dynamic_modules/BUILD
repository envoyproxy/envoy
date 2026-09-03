load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "data_input_lib",
    srcs = ["data_input.cc"],
    hdrs = ["data_input.h"],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//envoy/matcher:matcher_interface",
        "@envoy_api//envoy/extensions/matching/http/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "string_data_input_lib",
    srcs = ["string_data_input.cc"],
    hdrs = ["string_data_input.h"],
    deps = [
        "//envoy/common:exception_lib",
        "//envoy/common:optref_lib",
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//envoy/matcher:matcher_interface",
        "//envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules/abi",
        "@envoy_api//envoy/extensions/matching/http/dynamic_modules/v3:pkg_cc_proto",
    ],
)
