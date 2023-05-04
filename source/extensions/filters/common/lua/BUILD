load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)
load("//bazel:envoy_internal.bzl", "envoy_external_dep_path")
load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

licenses(["notice"])  # Apache 2

envoy_extension_package()

bool_flag(
    name = "luajit2",
    build_setting_default = False,
)

config_setting(
    name = "with_luajit2",
    flag_values = {
        ":luajit2": "True",
    },
)

envoy_cc_library(
    name = "lua_lib",
    srcs = ["lua.cc"],
    hdrs = ["lua.h"],
    deps = [
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
        "//source/common/common:lock_guard_lib",
        "//source/common/common:thread_lib",
        "//source/common/protobuf",
    ] + select({
        ":with_luajit2": [envoy_external_dep_path("luajit2")],
        "//conditions:default": [envoy_external_dep_path("luajit")],
    }),
)

envoy_cc_library(
    name = "wrappers_lib",
    srcs = ["wrappers.cc"],
    hdrs = ["wrappers.h"],
    deps = [
        ":lua_lib",
        "//envoy/buffer:buffer_interface",
        "//source/common/common:hex_lib",
        "//source/common/protobuf",
    ],
)
