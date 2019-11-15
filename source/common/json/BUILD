licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "json_loader_lib",
    srcs = ["json_loader.cc"],
    hdrs = ["json_loader.h"],
    external_deps = [
        "rapidjson",
    ],
    deps = [
        "//include/envoy/json:json_object_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:utility_lib",
        "//source/common/protobuf:utility_lib",
    ],
)
