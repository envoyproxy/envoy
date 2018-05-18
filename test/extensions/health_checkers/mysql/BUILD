licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "mysql_test",
    srcs = ["mysql_test.cc"],
    extension_name = "envoy.health_checkers.mysql",
    deps = [
        "//source/extensions/health_checkers/mysql",
        "//source/extensions/health_checkers/mysql:utility",
        "//test/common/buffer:utility_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)
