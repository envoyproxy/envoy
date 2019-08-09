load("@envoy_api//bazel:api_build_system.bzl", "api_go_grpc_library", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_proto_library_internal(
    name = "orca",
    srcs = ["orca.proto"],
    has_services = 1,
    deps = [
        "//udpa/data/orca/v1:orca_load_report",
    ],
)

api_go_grpc_library(
    name = "orca",
    proto = ":orca",
    deps = [
        "//udpa/data/orca/v1:orca_load_report_go_proto",
    ],
)
