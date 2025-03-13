load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "dns_filter_lib",
    srcs = [
        "dns_filter.cc",
        "dns_filter_resolver.cc",
        "dns_filter_utils.cc",
        "dns_parser.cc",
    ],
    hdrs = [
        "dns_filter.h",
        "dns_filter_constants.h",
        "dns_filter_resolver.h",
        "dns_filter_utils.h",
        "dns_parser.h",
    ],
    deps = [
        "//bazel/foreign_cc:ares",
        "//envoy/buffer:buffer_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:address_interface",
        "//envoy/network:dns_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listener_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/common:safe_memcpy_lib",
        "//source/common/common:trie_lookup_table_lib",
        "//source/common/config:config_provider_lib",
        "//source/common/config:datasource_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/network/dns_resolver:dns_factory_util_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/upstream:cluster_manager_lib",
        "@envoy_api//envoy/extensions/filters/udp/dns_filter/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":dns_filter_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "@envoy_api//envoy/extensions/filters/udp/dns_filter/v3:pkg_cc_proto",
    ],
)
