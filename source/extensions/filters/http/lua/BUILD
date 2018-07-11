licenses(["notice"])  # Apache 2

# Lua scripting L7 HTTP filter (https://www.lua.org/, http://luajit.org/)
# Public docs: docs/root/configuration/http_filters/lua_filter.rst

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
        "//source/common/http:message_lib",
        "//source/extensions/filters/common/lua:lua_lib",
        "//source/extensions/filters/common/lua:wrappers_lib",
        "//source/extensions/filters/http:well_known_names",
    ],
)

envoy_cc_library(
    name = "wrappers_lib",
    srcs = ["wrappers.cc"],
    hdrs = ["wrappers.h"],
    deps = [
        "//include/envoy/http:header_map_interface",
        "//include/envoy/request_info:request_info_interface",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/common/lua:lua_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//source/common/config:filter_json_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/lua:lua_filter_lib",
    ],
)
