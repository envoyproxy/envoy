load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "provider_singleton_lib",
    srcs = ["provider_singleton.cc"],
    hdrs = ["provider_singleton.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/ratelimit:ratelimit_interface",
        "//source/common/common:thread_synchronizer_lib",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/config:subscription_base_interface",
        "//source/common/grpc:common_lib",
        "//source/common/init:target_lib",
        "//source/extensions/filters/common/local_ratelimit:local_ratelimit_lib",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        ":provider_singleton_lib",
        "//envoy/access_log:access_log_interface",
        "//source/common/init:target_lib",
        "@envoy_api//envoy/extensions/access_loggers/filters/process_ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":filter_lib",
        "//envoy/access_log:access_log_interface",
        "//envoy/registry",
        "//source/common/access_log:access_log_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/access_loggers/filters/process_ratelimit/v3:pkg_cc_proto",
    ],
)
