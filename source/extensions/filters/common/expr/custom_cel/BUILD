load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "custom_cel_vocabulary_lib",
    hdrs = ["custom_cel_vocabulary.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf",
        "@com_google_cel_cpp//eval/public:activation",
        "@com_google_cel_cpp//eval/public:cel_function_registry",
    ],
)
