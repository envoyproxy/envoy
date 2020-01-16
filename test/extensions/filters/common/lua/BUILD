licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "lua_test",
    srcs = ["lua_test.cc"],
    deps = [
        "//source/extensions/filters/common/lua:lua_lib",
        "//test/mocks:common_lib",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "wrappers_test",
    srcs = ["wrappers_test.cc"],
    deps = [
        ":lua_wrappers_lib",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/common/lua:wrappers_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "lua_wrappers_lib",
    hdrs = ["lua_wrappers.h"],
    deps = [
        "//source/extensions/filters/common/lua:lua_lib",
        "//test/mocks/thread_local:thread_local_mocks",
    ],
)
