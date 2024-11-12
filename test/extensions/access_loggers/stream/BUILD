load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "stdout_config_test",
    srcs = ["stdout_config_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":stream_test_base",
        "//source/common/formatter:formatter_extension_lib",
        "@envoy_api//envoy/extensions/access_loggers/stream/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "stderr_config_test",
    srcs = ["stderr_config_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":stream_test_base",
        "//source/common/formatter:formatter_extension_lib",
        "@envoy_api//envoy/extensions/access_loggers/stream/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "stream_test_base",
    hdrs = ["stream_test_base.h"],
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/access_loggers/stream:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/stream/v3:pkg_cc_proto",
    ],
)
