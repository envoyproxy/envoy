load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "cel_lib",
    srcs = ["cel.cc"],
    hdrs = ["cel.h"],
    copts = [
        "-DUSE_CEL_PARSER",
    ],
    deps = [
        "//source/common/formatter:serializer_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "@cel-cpp//eval/public:value_export_util",
        "@cel-cpp//parser",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    copts = [
        "-DUSE_CEL_PARSER",
    ],
    # Ensure this could be used in tests.
    extra_visibility = [
        "//test:__subpackages__",
    ],
    deps = [
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//source/extensions/formatter/cel:cel_lib",
        "@cel-cpp//parser",
        "@envoy_api//envoy/extensions/formatter/cel/v3:pkg_cc_proto",
    ],
)
