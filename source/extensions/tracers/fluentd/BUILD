load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Trace driver for Fluentd: https://www.fluentd.org/

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":fluentd_tracer_lib",
        "//source/extensions/tracers/common:factory_base_lib",
        "@envoy_api//envoy/extensions/tracers/fluentd/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "fluentd_tracer_lib",
    srcs = ["fluentd_tracer_impl.cc"],
    hdrs = ["fluentd_tracer_impl.h"],
    tags = ["debug"],
    deps = [
        "//envoy/thread_local:thread_local_interface",
        "//source/common/config:utility_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/common/tracing:trace_context_lib",
        "//source/extensions/common/fluentd:fluentd_base_lib",
        "//source/extensions/tracers/common:factory_base_lib",
        "@com_github_msgpack_cpp//:msgpack",
        "@envoy_api//envoy/extensions/tracers/fluentd/v3:pkg_cc_proto",
    ],
)
