load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":router_interface",
        ":router_lib",
        "//contrib/sip_proxy/filters/network/source/filters:factory_base_lib",
        "//contrib/sip_proxy/filters/network/source/filters:filter_config_interface",
        "//contrib/sip_proxy/filters/network/source/filters:well_known_names",
        "//envoy/registry",
        "@envoy_api//contrib/envoy/extensions/filters/network/sip_proxy/router/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "router_interface",
    hdrs = ["router.h"],
    deps = [
        "//contrib/sip_proxy/filters/network/source:metadata_lib",
        "//envoy/router:router_interface",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "router_lib",
    srcs = ["router_impl.cc"],
    hdrs = ["router_impl.h"],
    deps = [
        ":router_interface",
        "//contrib/sip_proxy/filters/network/source:app_exception_lib",
        "//contrib/sip_proxy/filters/network/source:conn_manager_lib",
        "//contrib/sip_proxy/filters/network/source:conn_state_lib",
        "//contrib/sip_proxy/filters/network/source:decoder_lib",
        "//contrib/sip_proxy/filters/network/source:encoder_lib",
        "//contrib/sip_proxy/filters/network/source:utility_interface",
        "//contrib/sip_proxy/filters/network/source/filters:factory_base_lib",
        "//contrib/sip_proxy/filters/network/source/filters:filter_interface",
        "//contrib/sip_proxy/filters/network/source/filters:well_known_names",
        "//contrib/sip_proxy/filters/network/source/tra:tra_lib",
        "//envoy/tcp:conn_pool_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:thread_local_cluster_interface",
        "//source/common/common:logger_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/router:metadatamatchcriteria_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/sip_proxy/tra/v3alpha:pkg_cc_proto",
        "@envoy_api//contrib/envoy/extensions/filters/network/sip_proxy/v3alpha:pkg_cc_proto",
    ],
)
