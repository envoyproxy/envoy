load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "rds_test",
    srcs = ["rds_test.cc"],
    deps = [
        "//source/common/rds:rds_lib",
        "//test/mocks/server:instance_mocks",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)
