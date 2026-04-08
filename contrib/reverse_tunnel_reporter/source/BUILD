load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "reverse_tunnel_event_types",
    hdrs = [
        "reverse_tunnel_event_types.h",
    ],
    deps = [
        "//envoy/common:pure_lib",
        "//envoy/common:time_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/extensions/bootstrap/reverse_tunnel:reverse_tunnel_reporter_lib",
        "//envoy/server:factory_context_interface",
        "//source/common/common:fmt_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:message_validator_lib",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    deps = [
        "//contrib/reverse_tunnel_reporter/source/reporters:reporters_lib",
    ],
)
