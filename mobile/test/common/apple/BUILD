load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_test(
    name = "utility_test",
    srcs = select({
        "@envoy//bazel:apple": [
            "utility_test.cc",
        ],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    deps = select({
        "@envoy//bazel:apple": [
            "//library/common/apple:utility_lib",
        ],
        "//conditions:default": [],
    }),
)
