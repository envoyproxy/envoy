load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "health_checker_lib",
    srcs = ["health_checker_impl.cc"],
    hdrs = ["health_checker_impl.h"],
    deps = [
        "//envoy/api:io_error_interface",
        "//envoy/common:exception_lib",
        "//envoy/event:file_event_interface",
        "//envoy/event:schedulable_cb_interface",
        "//envoy/network:socket_interface",
        "//envoy/registry",
        "//envoy/server:health_checker_config_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/network:socket_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:health_checker_lib",
        "//source/extensions/health_checkers/common:health_checker_base_lib",
        "@abseil-cpp//absl/status",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/health_checkers/udp/v3:pkg_cc_proto",
    ],
)
