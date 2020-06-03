load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)
load("//bazel:envoy_internal.bzl", "envoy_external_dep_path")
load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

licenses(["notice"])  # Apache 2

envoy_package()

bool_flag(
    name = "moonjit",
    build_setting_default = False,
)

config_setting(
    name = "with_moonjit",
    flag_values = {
        ":moonjit": "True",
    },
)

envoy_cc_library(
    name = "lua_lib",
    srcs = ["lua.cc"],
    hdrs = ["lua.h"],
    deps = [
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
        "//source/common/protobuf",
    ] + select({
        ":with_moonjit": [envoy_external_dep_path("moonjit")],
        "//conditions:default": [envoy_external_dep_path("luajit")],
    }),
)

envoy_cc_library(
    name = "wrappers_lib",
    srcs = ["wrappers.cc"],
    hdrs = ["wrappers.h"],
    deps = [
        ":lua_lib",
        "//include/envoy/buffer:buffer_interface",
        "//source/common/protobuf",
    ],
)
