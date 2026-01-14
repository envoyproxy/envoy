load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "cluster_config_lib",
    srcs = ["cluster_config.cc"],
    hdrs = ["cluster_config.h"],
    deps = [
        ":abi",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_library(
    name = "abi",
    hdrs = ["abi.h"],
    deps = [
        "//source/extensions/dynamic_modules:abi",
    ],
)

# Export the abi.h header for use by test modules.
exports_files(["abi.h"])

envoy_cc_library(
    name = "cluster_lib",
    srcs = [
        "abi_impl.cc",
        "cluster.cc",
    ],
    hdrs = ["cluster.h"],
    deps = [
        ":abi",
        ":cluster_config_lib",
        "//envoy/upstream:cluster_factory_interface",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["factory.cc"],
    hdrs = ["factory.h"],
    deps = [
        ":cluster_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/clusters/dynamic_modules/v3:pkg_cc_proto",
    ],
)
