load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "cookie_test",
    srcs = ["cookie_test.cc"],
    extension_names = ["envoy.http.stateful_session.cookie"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:utility_lib",
        "//source/extensions/http/stateful_session/cookie:cookie_lib",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.http.stateful_session.cookie"],
    rbe_pool = "2core",
    deps = [
        "//envoy/registry",
        "//source/extensions/http/stateful_session/cookie:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)
