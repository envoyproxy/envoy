load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# On-demand RDS update HTTP filter

envoy_extension_package()

envoy_cc_library(
    name = "on_demand_update_lib",
    srcs = ["on_demand_update.cc"],
    hdrs = ["on_demand_update.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/http:filter_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:enum_to_int",
        "//source/common/config:xds_resource_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http:well_known_names",
        "@envoy_api//envoy/extensions/filters/http/on_demand/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/on_demand:on_demand_update_lib",
        "@envoy_api//envoy/extensions/filters/http/on_demand/v3:pkg_cc_proto",
    ],
)
