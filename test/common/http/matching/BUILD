load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "inputs_test",
    srcs = ["inputs_test.cc"],
    deps = [
        "//source/common/http/matching:data_impl_lib",
        "//source/common/http/matching:inputs_lib",
    ],
)
