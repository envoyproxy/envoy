package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "connection_lib",
    srcs = ["connection_impl.cc"],
    hdrs = ["connection_impl.h"],
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
    deps = [
        "//include/envoy/ssl:context_config_includes",
        "//source/common/json:json_loader_lib",
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
    deps = [
        ":openssl_lib",
        "//include/envoy/runtime:runtime_includes",
        "//include/envoy/ssl:context_config_includes",
        "//include/envoy/ssl:context_includes",
        "//include/envoy/ssl:context_manager_includes",
        "//include/envoy/stats:stats_includes",
        "//include/envoy/stats:stats_macros",
        "//source/common/common:assert_lib",
        "//source/common/common:hex_lib",
    ],
)

envoy_cc_library(
    name = "openssl_lib",
    srcs = ["openssl.cc"],
    hdrs = ["openssl.h"],
    external_deps = ["ssl"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
    ],
)
