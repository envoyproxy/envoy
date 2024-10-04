load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "factory_test",
    srcs = ["factory_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:no_op",
    ],
    deps = [
        "//source/extensions/filters/http/dynamic_modules:factory_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)
