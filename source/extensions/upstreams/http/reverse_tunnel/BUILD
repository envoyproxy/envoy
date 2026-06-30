load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
        "drain_registry.cc",
    ],
    hdrs = [
        "config.h",
        "drain_aware_client_connection.h",
        "drain_aware_http2_client_connection.h",
        "drain_registry.h",
        "reverse_tunnel_codec_stats.h",
    ],
    deps = [
        "//envoy/http:client_codec_factory_interface",
        "//envoy/http:codec_interface",
        "//envoy/http:codes_interface",
        "//envoy/registry",
        "//envoy/server:admin_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/server:options_interface",
        "//envoy/singleton:instance_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/upstreams/http/reverse_tunnel/v3:pkg_cc_proto",
    ],
)
