licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "connection_lib",
    srcs = ["connection_impl.cc"],
    hdrs = ["connection_impl.h"],
    external_deps = ["ssl"],
    deps = [
        ":context_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:hex_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
    ],
)

envoy_cc_library(
    name = "context_config_lib",
    srcs = ["context_config_impl.cc"],
    hdrs = ["context_config_impl.h"],
    external_deps = ["envoy_sds"],
    deps = [
        "//include/envoy/ssl:context_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/config:tls_context_json_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "context_lib",
    srcs = [
        "context_impl.cc",
        "context_manager_impl.cc",
    ],
    hdrs = [
        "context_impl.h",
        "context_manager_impl.h",
    ],
    external_deps = ["ssl"],
    deps = [
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/ssl:context_config_interface",
        "//include/envoy/ssl:context_interface",
        "//include/envoy/ssl:context_manager_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:hex_lib",
    ],
)
