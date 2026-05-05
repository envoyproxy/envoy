load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "event_reporter_test",
    srcs = ["event_reporter_test.cc"],
    deps = [
        "//contrib/reverse_tunnel_reporter/source/reporters/event_reporter:event_reporter_lib",
        "//source/common/api:api_lib",
        "//source/common/config:utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
    ],
)
