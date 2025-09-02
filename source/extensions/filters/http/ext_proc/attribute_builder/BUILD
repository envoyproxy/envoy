load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "attribute_builder_interface",
    hdrs = ["attribute_builder.h"],
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/config:typed_config_interface",
        "//envoy/server:factory_context_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "default_attribute_builder_lib",
    srcs = ["default_attribute_builder.cc"],
    hdrs = ["default_attribute_builder.h"],
    deps = [
        ":attribute_builder_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//source/extensions/filters/http/ext_proc:matching_utils_lib",
    ],
)
