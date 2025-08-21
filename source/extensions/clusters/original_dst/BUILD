load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "original_dst_cluster_lib",
    srcs = ["original_dst_cluster.cc"],
    hdrs = ["original_dst_cluster.h"],
    # prevously considered core code.
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/secret:secret_manager_interface",
        "//envoy/upstream:cluster_factory_interface",
        "//source/common/common:empty_string",
        "//source/common/network:address_lib",
        "//source/common/network:filter_state_dst_address_lib",
        "//source/common/network:utility_lib",
        "//source/common/runtime:runtime_features_lib",
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:upstream_includes",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)
