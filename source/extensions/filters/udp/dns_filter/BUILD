load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "dns_filter_lib",
    srcs = [
        "dns_filter.cc",
        "dns_filter_resolver.cc",
        "dns_parser.cc",
    ],
    hdrs = [
        "dns_filter.h",
        "dns_filter_resolver.h",
        "dns_parser.h",
    ],
    external_deps = ["ares"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:address_interface",
        "//include/envoy/network:dns_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listener_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/common:matchers_lib",
        "//source/common/config:config_provider_lib",
        "//source/common/config:datasource_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/upstream:cluster_manager_lib",
        "@envoy_api//envoy/extensions/filters/udp/dns_filter/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    status = "alpha",
    deps = [
        ":dns_filter_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "@envoy_api//envoy/extensions/filters/udp/dns_filter/v3alpha:pkg_cc_proto",
    ],
)
