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
    name = "header_test",
    srcs = ["header_test.cc"],
    extension_names = ["envoy.http.stateful_session.header"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:utility_lib",
        "//source/extensions/http/stateful_session/header:header_lib",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.http.stateful_session.header"],
    rbe_pool = "2core",
    deps = [
        "//envoy/registry",
        "//source/extensions/http/stateful_session/header:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)
