licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//source/common/access_log:grpc_access_log_lib",
        "//source/server/config/access_log:grpc_access_log_lib",
        "//test/mocks/server:server_mocks",
    ],
)
