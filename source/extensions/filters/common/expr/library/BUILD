load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "custom_functions_lib",
    srcs = ["custom_functions.cc"],
    hdrs = ["custom_functions.h"],
    deps = [
        "//source/common/protobuf",
        "@com_google_cel_cpp//eval/public:cel_function",
        "@com_google_cel_cpp//eval/public:cel_value",
    ],
)

envoy_cc_library(
    name = "custom_vocabulary_lib",
    srcs = ["custom_vocabulary.cc"],
    hdrs = ["custom_vocabulary.h"],
    copts = [
        "-Wno-unused-parameter",
        "-Wno-unused-private-field",
    ],
    deps = [
        "//source/common/protobuf",
        "//source/extensions/filters/common/expr:context_lib",
        "@com_google_cel_cpp//eval/public:cel_value",
    ],
)

envoy_cc_extension(
    name = "custom_library_lib",
    srcs = ["custom_library.cc"],
    hdrs = [
        "custom_library.h",
    ],
    copts = [
        "-Wno-unused-parameter",
    ],
    deps = [
        ":custom_functions_lib",
        ":custom_vocabulary_lib",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/expr:context_lib",
        "@com_google_cel_cpp//eval/public:activation",
        "@com_google_cel_cpp//eval/public:builtin_func_registrar",
        "@com_google_cel_cpp//eval/public:cel_expr_builder_factory",
        "@com_google_cel_cpp//eval/public:cel_expression",
        "@com_google_cel_cpp//eval/public:cel_function",
        "@com_google_cel_cpp//eval/public:cel_value",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/rbac/custom_library_config/v3:pkg_cc_proto",
    ],
)
