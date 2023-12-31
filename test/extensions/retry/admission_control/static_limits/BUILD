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
    extension_names = ["envoy.retry_admission_control.static_limits"],
    deps = [
        "//source/extensions/retry/admission_control/static_limits:config",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/retry/admission_control/static_limits/v3:pkg_cc_proto",
    ],
)
