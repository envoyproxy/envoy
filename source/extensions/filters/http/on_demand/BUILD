licenses(["notice"])  # Apache 2

# On-demand RDS update HTTP filter

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "on_demand_update_lib",
    srcs = ["on_demand_update.cc"],
    hdrs = ["on_demand_update.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/server:filter_config_interface",
        "//source/common/common:enum_to_int",
        "//source/common/http:header_map_lib",
        "//source/common/common:assert_lib",
        "//source/common/http:codes_lib",
    ],
)
