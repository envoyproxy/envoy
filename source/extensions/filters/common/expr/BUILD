licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "evaluator_lib",
    srcs = ["evaluator.cc"],
    hdrs = ["evaluator.h"],
    deps = [
        ":context_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf",
        "@com_google_cel_cpp//eval/public:builtin_func_registrar",
        "@com_google_cel_cpp//eval/public:cel_expr_builder_factory",
        "@com_google_cel_cpp//eval/public:cel_expression",
        "@com_google_cel_cpp//eval/public:cel_value",
    ],
)

envoy_cc_library(
    name = "context_lib",
    srcs = ["context.cc"],
    hdrs = ["context.h"],
    deps = [
        "//source/common/grpc:common_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:utility_lib",
        "//source/common/stream_info:utility_lib",
        "@com_google_cel_cpp//eval/public:cel_value",
        "@com_google_cel_cpp//eval/public:cel_value_producer",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
