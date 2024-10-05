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
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:server_factory_context_mocks",
    ],
)

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/rust:no_op",
    ],
    deps = [
        "//source/extensions/filters/http/dynamic_modules:filter_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:server_mocks",
    ],
)
