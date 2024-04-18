load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "evaluator_lib",
    srcs = ["evaluator.cc"],
    hdrs = ["evaluator.h"],
    tags = ["skip_on_windows"],
    deps = [
        ":context_lib",
        "//envoy/singleton:manager_interface",
        "//source/common/http:utility_lib",
        "//source/common/protobuf",
        "@com_google_cel_cpp//eval/public:activation",
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
    tags = ["skip_on_windows"],
    deps = [
        ":cel_state_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:utility_lib",
        "//source/common/singleton:const_singleton",
        "//source/common/stream_info:utility_lib",
        "@com_google_cel_cpp//eval/public:cel_value",
        "@com_google_cel_cpp//eval/public:cel_value_producer",
        "@com_google_cel_cpp//eval/public/containers:container_backed_list_impl",
        "@com_google_cel_cpp//eval/public/structs:cel_proto_wrapper",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "cel_state_lib",
    srcs = [
        "cel_state.cc",
    ],
    hdrs = [
        "cel_state.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/stream_info:filter_state_interface",
        "//source/common/protobuf",
        "//source/common/singleton:const_singleton",
        "@com_github_google_flatbuffers//:flatbuffers",
        "@com_google_cel_cpp//eval/public:cel_value",
        "@com_google_cel_cpp//eval/public/structs:cel_proto_wrapper",
        "@com_google_cel_cpp//tools:flatbuffers_backed_impl",
    ],
)
