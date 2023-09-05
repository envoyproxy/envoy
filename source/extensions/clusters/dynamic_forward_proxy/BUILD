load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "cluster",
    srcs = ["cluster.cc"],
    hdrs = ["cluster.h"],
    deps = [
        "//envoy/router:string_accessor_interface",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:uint32_accessor_lib",
        "//source/common/upstream:cluster_factory_lib",
        "//source/extensions/clusters/common:logical_host_lib",
        "//source/extensions/common/dynamic_forward_proxy:cluster_store",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_interface",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_manager_impl",
        "//source/extensions/transport_sockets/tls/cert_validator:cert_validator_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)
