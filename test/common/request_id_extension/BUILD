

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
        "//test/test_common:utility_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//source/common/runtime:runtime_lib",
        "//source/common/request_id_extension:request_id_extension_lib",
    ],
)
