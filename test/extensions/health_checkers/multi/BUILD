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
    name = "multi_test",
    srcs = ["multi_test.cc"],
    extension_names = ["envoy.health_checkers.multi"],
    deps = [
        "//source/extensions/health_checkers/http:health_checker_lib",
        "//source/extensions/health_checkers/multi:health_checker_lib",
        "//source/extensions/health_checkers/tcp:health_checker_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/server:health_checker_factory_context_mocks",
        "//test/mocks/upstream:priority_set_mocks",
    ],
)
