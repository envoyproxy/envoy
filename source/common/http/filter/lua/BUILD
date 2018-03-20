licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "lua_filter_lib",
    srcs = ["lua_filter.cc"],
    hdrs = ["lua_filter.h"],
    deps = [
        ":wrappers_lib",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:enum_to_int",
        "//source/common/config:well_known_names",
        "//source/common/http:message_lib",
        "//source/common/lua:lua_lib",
        "//source/common/lua:wrappers_lib",
    ],
)

envoy_cc_library(
    name = "wrappers_lib",
    srcs = ["wrappers.cc"],
    hdrs = ["wrappers.h"],
    deps = [
        "//include/envoy/http:header_map_interface",
        "//source/common/config:well_known_names",
        "//source/common/lua:lua_lib",
    ],
)
