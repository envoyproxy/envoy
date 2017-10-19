licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "lua_lib",
    srcs = ["lua.cc"],
    hdrs = ["lua.h"],
    external_deps = [
        "luajit",
    ],
    deps = [
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
    ],
)
