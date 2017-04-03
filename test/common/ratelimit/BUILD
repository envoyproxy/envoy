package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_test")

envoy_cc_test(
    name = "ratelimit_impl_test",
    srcs = ["ratelimit_impl_test.cc"],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/ratelimit:ratelimit_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)
