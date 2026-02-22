load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "cluster_lib",
    srcs = [
        "abi_impl.cc",
        "cluster.cc",
    ],
    hdrs = [
        "cluster.h",
    ],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "cluster",
    deps = [
        ":cluster_lib",
    ],
)
