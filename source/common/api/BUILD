load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "api_lib",
    srcs = ["api_impl.cc"],
    hdrs = ["api_impl.h"],
    deps = [
        "//include/envoy/api:api_interface",
        "//source/common/event:dispatcher_lib",
        "//source/common/filesystem:filesystem_lib",
    ],
)
