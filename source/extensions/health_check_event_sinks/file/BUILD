load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "file_sink_lib",
    srcs = ["file_sink_impl.cc"],
    hdrs = ["file_sink_impl.h"],
    extra_visibility = [
        # previously considered core code.
        "//envoy/src/test:__subpackages__",
    ],
    deps = [
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//envoy/upstream:health_check_event_sink_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/health_check_event_sinks/file/v3:pkg_cc_proto",
    ],
)