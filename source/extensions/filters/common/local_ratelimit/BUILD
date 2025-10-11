load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "local_ratelimit_interface",
    hdrs = ["local_ratelimit.h"],
    deps = [
        "//envoy/ratelimit:ratelimit_interface",
    ],
)

envoy_cc_library(
    name = "local_ratelimit_lib",
    srcs = ["local_ratelimit_impl.cc"],
    hdrs = ["local_ratelimit_impl.h"],
    deps = [
        ":local_ratelimit_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/ratelimit:ratelimit_interface",
        "//source/common/common:thread_synchronizer_lib",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/common/ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "provider_singleton_lib",
    srcs = ["provider_singleton.cc"],
    hdrs = ["provider_singleton.h"],
    deps = [
        ":local_ratelimit_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/ratelimit:ratelimit_interface",
        "//source/common/common:thread_synchronizer_lib",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/config:subscription_base_interface",
        "//source/common/grpc:common_lib",
        "//source/common/init:target_lib",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)
