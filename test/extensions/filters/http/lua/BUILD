licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "lua_filter_test",
    srcs = ["lua_filter_test.cc"],
    deps = [
        "//source/extensions/filters/http/lua:lua_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "wrappers_test",
    srcs = ["wrappers_test.cc"],
    deps = [
        "//source/extensions/filters/http/lua:wrappers_lib",
        "//test/extensions/filters/common/lua:lua_wrappers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "lua_integration_test",
    srcs = ["lua_integration_test.cc"],
    deps = [
        "//source/extensions/filters/http/lua:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//source/extensions/filters/http/lua:config",
        "//test/mocks/server:server_mocks",
    ],
)
