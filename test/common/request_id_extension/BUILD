licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "uuid_impl_test",
    srcs = ["uuid_impl_test.cc"],
    deps = [
        "//source/common/request_id_extension:request_id_extension_lib",
        "//source/common/runtime:runtime_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:utility_lib",
    ],
)
