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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.access_loggers.extension_filters.cel"],
    deps = [
        "//source/extensions/access_loggers/filters/cel:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/filters/cel/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "cel_test",
    srcs = ["cel_test.cc"],
    extension_names = ["envoy.access_loggers.extension_filters.cel"],
    deps = [
        "//source/extensions/access_loggers/filters/cel:cel_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@com_google_cel_cpp//parser",
    ],
)
