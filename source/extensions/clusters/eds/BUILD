load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "eds_lib",
    srcs = ["eds.cc"],
    hdrs = ["eds.h"],
    visibility = ["//visibility:public"],
    deps = [
        "leds_lib",
        "//envoy/config:grpc_mux_interface",
        "//envoy/config:subscription_factory_interface",
        "//envoy/config:subscription_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/secret:secret_manager_interface",
        "//envoy/upstream:cluster_factory_interface",
        "//envoy/upstream:locality_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:decoded_resource_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:subscription_base_interface",
        "//source/common/config:subscription_factory_lib",
        "//source/common/config:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:upstream_includes",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "leds_lib",
    srcs = ["leds.cc"],
    hdrs = ["leds.h"],
    deps = [
        "//envoy/config:grpc_mux_interface",
        "//envoy/config:subscription_factory_interface",
        "//envoy/config:subscription_interface",
        "//envoy/local_info:local_info_interface",
        "//source/common/config:decoded_resource_lib",
        "//source/common/config:subscription_base_interface",
        "//source/common/config:subscription_factory_lib",
        "//source/common/config:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:upstream_includes",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)
