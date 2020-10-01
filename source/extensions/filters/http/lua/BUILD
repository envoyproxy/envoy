load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Lua scripting L7 HTTP filter (https://www.lua.org/, http://luajit.org/)
# Public docs: docs/root/configuration/http_filters/lua_filter.rst

envoy_extension_package()

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
        "//source/common/config:datasource_lib",
        "//source/common/crypto:utility_lib",
        "//source/common/http:message_lib",
        "//source/extensions/common:utility_lib",
        "//source/extensions/filters/common/lua:lua_lib",
        "//source/extensions/filters/common/lua:wrappers_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/lua/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "wrappers_lib",
    srcs = ["wrappers.cc"],
    hdrs = ["wrappers.h"],
    deps = [
        "//include/envoy/http:header_map_interface",
        "//include/envoy/stream_info:stream_info_interface",
        "//source/common/crypto:utility_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/common/crypto:utility_lib",
        "//source/extensions/filters/common/lua:lua_lib",
        "//source/extensions/filters/common/lua:wrappers_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        "//include/envoy/registry",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/lua:lua_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/lua/v3:pkg_cc_proto",
    ],
)
