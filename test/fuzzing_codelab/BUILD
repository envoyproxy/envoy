licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "date_formatter_lib",
    srcs = ["date_formatter.cc"],
    hdrs = ["date_formatter.h"],
    deps = [
        "//include/envoy/common:time_interface",
        "//source/common/common:assert_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_test(
    name = "date_formatter_test",
    srcs = ["date_formatter_test.cc"],
    external_deps = [
        "abseil_strings",
    ],
    deps = [
        ":date_formatter_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "date_formatter_fuzz_test",
    srcs = ["date_formatter_fuzz_test.cc"],
    corpus = "date_formatter_corpus",
    deps = [
        ":date_formatter_lib",
    ],
)
