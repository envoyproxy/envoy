licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "local_reply_lib",
    srcs = ["local_reply.cc"],
    hdrs = ["local_reply.h"],
    deps = [
        "//include/envoy/http:codes_interface",
        "//source/common/access_log:access_log_formatter_lib",
    ],
)
