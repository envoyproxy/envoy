load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "custom_cel_functions_lib",
    srcs = [
        "custom_cel_functions.cc",
    ],
    hdrs = ["custom_cel_functions.h"],
    deps = [
        "//source/common/protobuf",
        "//source/extensions/filters/common/expr/custom_cel:custom_cel_vocabulary_lib",
        "@com_google_cel_cpp//eval/public:cel_function",
        "@com_google_cel_cpp//eval/public:cel_value",
    ],
)

envoy_cc_library(
    name = "custom_cel_variables_lib",
    srcs = ["custom_cel_variables.cc"],
    hdrs = ["custom_cel_variables.h"],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf",
        "//source/common/stream_info:utility_lib",
        "//source/extensions/filters/common/expr:context_lib",
        "//source/extensions/filters/common/expr/custom_cel:custom_cel_vocabulary_lib",
        "@com_google_cel_cpp//eval/public:cel_value",
    ],
)

envoy_cc_extension(
    name = "example_custom_cel_vocabulary_lib",
    srcs = ["example_custom_cel_vocabulary.cc"],
    hdrs = [
        "example_custom_cel_vocabulary.h",
    ],
    deps = [
        ":custom_cel_functions_lib",
        ":custom_cel_variables_lib",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/expr:context_lib",
        "//source/extensions/filters/common/expr/custom_cel:custom_cel_vocabulary_lib",
        "@com_google_cel_cpp//eval/public:activation",
        "@com_google_cel_cpp//eval/public:cel_function",
        "@com_google_cel_cpp//eval/public:cel_function_adapter",
        "@com_google_cel_cpp//eval/public:cel_function_registry",
        "@com_google_cel_cpp//eval/public:cel_value",
        "@envoy_api//envoy/extensions/expr/custom_cel_vocabulary/example/v3:pkg_cc_proto",
    ],
)
