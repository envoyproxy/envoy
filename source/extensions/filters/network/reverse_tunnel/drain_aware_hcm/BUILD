load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "drain_aware_config",
    srcs = ["drain_aware_config.cc"],
    hdrs = [
        "drain_aware_config.h",
        "drain_aware_server_connection.h",
    ],
    deps = [
        "//envoy/common:exception_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/http:codec_interface",
        "//envoy/network:drain_decision_interface",
        "//envoy/server:factory_context_interface",
        "//source/common/http:conn_manager_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/http_connection_manager:config",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/reverse_tunnel/v3:pkg_cc_proto",
    ],
)
