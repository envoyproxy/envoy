licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "conveyor_test",
    srcs = ["conveyor_test.cc"],
    deps = [
        "//source/common/common:thread_lib",
        "//source/common/log_sink:conveyor_lib",
    ],
)

envoy_cc_test(
    name = "log_obj_store_test",
    srcs = ["log_obj_store_test.cc"],
    deps = [
        "//source/common/log_sink:log_obj_store_lib",
    ],
)
