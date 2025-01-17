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
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/ratelimit:ratelimit_interface",
        "//source/common/common:thread_synchronizer_lib",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/common/ratelimit/v3:pkg_cc_proto",
    ],
)
