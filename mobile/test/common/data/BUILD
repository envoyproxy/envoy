load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common/data:utility_lib",
        "//library/common/types:c_types_lib",
        "@envoy//source/common/buffer:buffer_lib",
    ],
)
