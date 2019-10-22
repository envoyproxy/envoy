licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_cc_test(
    name = "watcher_impl_test",
    srcs = ["watcher_impl_test.cc"],
    deps = [
        "//test/mocks/init:init_mocks",
    ],
)

envoy_cc_test(
    name = "target_impl_test",
    srcs = ["target_impl_test.cc"],
    deps = [
        "//test/mocks/init:init_mocks",
    ],
)

envoy_cc_test(
    name = "manager_impl_test",
    srcs = ["manager_impl_test.cc"],
    deps = [
        "//source/common/init:manager_lib",
        "//test/mocks/init:init_mocks",
    ],
)

envoy_proto_library(
    name = "init_fuzz_proto",
    srcs = ["init_fuzz.proto"],
    deps = ["//test/fuzz:common_proto"],
)

envoy_cc_fuzz_test(
    name = "init_fuzz_test",
    srcs = ["init_fuzz_test.cc"],
    corpus = "init_corpus",
    deps = [
        ":init_fuzz_proto_cc_proto",
        "//source/common/init:manager_lib",
        "//test/fuzz:utility_lib",
    ],
)
