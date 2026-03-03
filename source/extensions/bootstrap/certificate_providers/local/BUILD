load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/common:exception_lib",
        "//envoy/registry",
        "//envoy/server:bootstrap_extension_config_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/server:instance_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/transport_sockets/tls/cert_selectors/on_demand:config",
        "@envoy_api//envoy/extensions/bootstrap/certificate_providers/local/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3:pkg_cc_proto",
    ],
)
