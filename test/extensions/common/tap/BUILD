load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_select_admin_functionality",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "common",
    srcs = ["common.cc"],
    hdrs = ["common.h"],
    deps = [
        "//source/extensions/common/tap:tap_interface",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "admin_test",
    srcs = envoy_select_admin_functionality(["admin_test.cc"]),
    deps = [
        "//source/extensions/common/tap:admin",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:admin_stream_mocks",
        "//test/test_common:logging_lib",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "tap_config_base_test",
    srcs = ["tap_config_base_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/common/tap:tap_config_base",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
    ],
)
