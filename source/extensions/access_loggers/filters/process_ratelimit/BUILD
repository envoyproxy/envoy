load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        "//envoy/access_log:access_log_interface",
        "//source/common/init:target_lib",
        "//source/extensions/filters/common/local_ratelimit:provider_singleton_lib",
        "@envoy_api//envoy/extensions/access_loggers/filters/process_ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    extra_visibility = [
        "//test/common/access_log:__subpackages__",
        "//test/integration:__subpackages__",
    ],
    deps = [
        ":filter_lib",
        "//envoy/access_log:access_log_interface",
        "//envoy/registry",
        "//source/common/access_log:access_log_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/local_ratelimit:provider_singleton_lib",
        "@envoy_api//envoy/extensions/access_loggers/filters/process_ratelimit/v3:pkg_cc_proto",
    ],
)
