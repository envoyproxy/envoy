load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "lua_lib",
    srcs = ["lua.cc"],
    hdrs = ["lua.h"],
    deps = [
        "//bazel/foreign_cc:luajit",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
        "//source/common/common:lock_guard_lib",
        "//source/common/common:thread_lib",
        "//source/common/protobuf",
    ],
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
