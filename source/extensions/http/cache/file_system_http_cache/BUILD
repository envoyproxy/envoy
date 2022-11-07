load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "cache_file_fixed_block",
    srcs = ["cache_file_fixed_block.cc"],
    hdrs = ["cache_file_fixed_block.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:byte_order_lib",
        "//source/common/common:safe_memcpy_lib",
        "@com_google_absl//absl/strings",
    ],
)
