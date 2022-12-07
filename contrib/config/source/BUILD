load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_contrib_extension(
    name = "kv_store_xds_delegate",
    srcs = ["kv_store_xds_delegate.cc"],
    hdrs = ["kv_store_xds_delegate.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//envoy/common:key_value_store_interface",
        "//envoy/common:time_interface",
        "//envoy/config:xds_resources_delegate_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//contrib/envoy/extensions/config/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)
