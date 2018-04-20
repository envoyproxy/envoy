licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test_library(
    name = "main",
    srcs = ["main.cc"],
    deps = [
        ":fuzz_runner_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/filesystem:filesystem_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "fuzz_runner_lib",
    srcs = ["fuzz_runner.cc"],
    hdrs = ["fuzz_runner.h"],
    external_deps = ["libprotobuf_mutator"],
    deps = [
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/event:libevent_lib",
        "//test/test_common:environment_lib",
    ],
)
