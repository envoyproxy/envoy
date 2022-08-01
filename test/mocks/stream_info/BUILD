load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "stream_info_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//envoy/http:request_id_extension_interface",
        "//envoy/stream_info:stream_info_interface",
        "//envoy/upstream:upstream_interface",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:simulated_time_system_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
