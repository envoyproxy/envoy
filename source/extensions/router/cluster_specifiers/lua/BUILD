load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Lua cluster specifier plugin.

envoy_extension_package()

envoy_cc_library(
    name = "lua_cluster_specifier_lib",
    srcs = [
        "lua_cluster_specifier.cc",
    ],
    hdrs = [
        "lua_cluster_specifier.h",
    ],
    deps = [
        "//envoy/router:cluster_specifier_plugin_interface",
        "//source/common/common:utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/router:config_lib",
        "//source/common/runtime:runtime_features_lib",
        "//source/extensions/filters/common/lua:lua_lib",
        "//source/extensions/filters/common/lua:wrappers_lib",
        "@envoy_api//envoy/extensions/router/cluster_specifiers/lua/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":lua_cluster_specifier_lib",
        "//envoy/registry",
    ],
)
