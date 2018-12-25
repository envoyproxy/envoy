licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "admin_test",
    srcs = ["admin_test.cc"],
    deps = [
        "//source/extensions/common/tap:admin",
        "//test/mocks/server:server_mocks",
    ],
)
