load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP filter that implements the reverse_conn microservice debugger
# Public docs: docs/root/configuration/http_filters/reverse_conn_filter.rst

envoy_extension_package()

envoy_cc_library(
    name = "reverse_conn_filter_lib",
    srcs = ["reverse_conn_filter.cc"],
    hdrs = ["reverse_conn_filter.h"],
    deps = [
        "//envoy/event:timer_interface",
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:filter_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/reverse_conn/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    # category = "envoy.filters.http",
    # TODO move integration test to extensions.
    extra_visibility = [
        "//test/integration:__subpackages__",
    ],
    # security_posture = "robust_to_untrusted_downstream",
    deps = [
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/reverse_conn:reverse_conn_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/reverse_conn/v3:pkg_cc_proto",
    ],
)
