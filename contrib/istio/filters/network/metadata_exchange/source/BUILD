load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "metadata_exchange",
    srcs = [
        "metadata_exchange.cc",
        "metadata_exchange_initial_header.cc",
    ],
    hdrs = [
        "metadata_exchange.h",
        "metadata_exchange_initial_header.h",
    ],
    repository = "@envoy",
    deps = [
        "//contrib/istio/filters/common/source:metadata_object_lib",
        "//contrib/istio/filters/common/source:workload_discovery_lib",
        "//envoy/local_info:local_info_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/stats:stats_macros",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/http:utility_lib",
        "//source/common/network:filter_state_dst_address_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/stream_info:bool_accessor_lib",
        "//source/extensions/filters/common/expr:cel_state_lib",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/strings",
        "@envoy_api//contrib/envoy/extensions/filters/network/metadata_exchange/v3:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":metadata_exchange",
        "@envoy_api//contrib/envoy/extensions/filters/network/metadata_exchange/v3:pkg_cc_proto",
    ],
)
