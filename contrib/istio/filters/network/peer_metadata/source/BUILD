load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["peer_metadata.cc"],
    hdrs = ["peer_metadata.h"],
    deps = [
        "//contrib/istio/filters/common/source:metadata_object_lib",
        "//envoy/local_info:local_info_interface",
        "//envoy/network:address_interface",
        "//envoy/network:filter_interface",
        "//envoy/server:filter_config_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/singleton:const_singleton",
        "//source/common/stream_info:bool_accessor_lib",
        "//source/common/tcp_proxy",
        "//source/extensions/filters/common/expr:cel_state_lib",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/peer_metadata/v3:pkg_cc_proto",
    ],
)
