load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "local_ratelimit_lib",
    srcs = ["local_ratelimit_impl.cc"],
    hdrs = ["local_ratelimit_impl.h"],
    deps = [
        ":local_ratelimit_client_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
    ],
)

envoy_cc_library(
    name = "local_ratelimit_client_interface",
    hdrs = ["local_ratelimit.h"],
    deps = [
    ],
)
