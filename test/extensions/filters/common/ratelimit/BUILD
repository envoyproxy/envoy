load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "ratelimit_impl_test",
    srcs = ["ratelimit_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/filters/common/ratelimit:ratelimit_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/service/ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_cc_mock(
    name = "ratelimit_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//envoy/ratelimit:ratelimit_interface",
        "//source/extensions/filters/common/ratelimit:ratelimit_client_interface",
    ],
)

envoy_cc_test_library(
    name = "ratelimit_utils",
    hdrs = ["utils.h"],
    deps = [
        "@envoy_api//envoy/service/ratelimit/v3:pkg_cc_proto",
    ],
)
