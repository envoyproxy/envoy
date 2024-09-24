load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    engflow_pool = "2core",
    deps = [
        "//source/extensions/upstreams/http/generic:config",
        "//test/mocks:common_lib",
        "//test/mocks/router:router_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)
