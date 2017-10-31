licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "wrappers_test",
    srcs = ["wrappers_test.cc"],
    deps = [
        "//source/common/http/filter/lua:wrappers_lib",
        "//test/test_common:lua_wrappers_lib",
        "//test/test_common:utility_lib",
    ],
)
