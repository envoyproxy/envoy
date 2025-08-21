load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_library(
    name = "utility_lib",
    srcs = select({
        "@envoy//bazel:apple": [
            "utility.cc",
        ],
        "//conditions:default": [],
    }),
    hdrs = select({
        "@envoy//bazel:apple": [
            "utility.h",
        ],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    deps = select({
        "@envoy//bazel:apple": [
            "@envoy//source/common/common:assert_lib",
        ],
        "//conditions:default": [],
    }),
)
