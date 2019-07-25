licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

envoy_package()

envoy_cc_test(
    name = "header_utility_test",
    srcs = ["header_utility_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common/http:header_utility_lib",
        "//library/common/include:c_types_interface",
        "@envoy//source/common/http:header_map_lib",
    ],
)
