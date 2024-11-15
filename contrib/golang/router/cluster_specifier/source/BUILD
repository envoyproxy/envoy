load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

# Golang cluster specifier plugin.

envoy_contrib_package()

envoy_cc_library(
    name = "golang_cluster_specifier_lib",
    srcs = [
        "golang_cluster_specifier.cc",
    ],
    hdrs = [
        "golang_cluster_specifier.h",
    ],
    deps = [
        ":cgo",
        "//contrib/golang/common/dso:dso_lib",
        "//envoy/router:cluster_specifier_plugin_interface",
        "//source/common/common:utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/router:config_lib",
        "@envoy_api//contrib/envoy/extensions/router/cluster_specifier/golang/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":golang_cluster_specifier_lib",
        "//envoy/registry",
    ],
)

envoy_cc_library(
    name = "cgo",
    srcs = ["cgo.cc"],
    hdrs = [
        "golang_cluster_specifier.h",
    ],
    deps = [
        "//contrib/golang/common/dso:dso_lib",
        "//envoy/router:cluster_specifier_plugin_interface",
        "//source/common/common:utility_lib",
        "//source/common/http:utility_lib",
        "@envoy_api//contrib/envoy/extensions/router/cluster_specifier/golang/v3alpha:pkg_cc_proto",
    ],
)
