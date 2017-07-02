licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "filesystem_subscription_lib",
    hdrs = ["filesystem_subscription_impl.h"],
    external_deps = ["protobuf"],
    deps = [
        "//include/envoy/config:subscription_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/filesystem:filesystem_interface",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/filesystem:filesystem_lib",
    ],
)

envoy_cc_library(
    name = "grpc_subscription_lib",
    hdrs = ["grpc_subscription_impl.h"],
    external_deps = ["envoy_base"],
    deps = [
        "//include/envoy/config:subscription_interface",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/grpc:async_client_lib",
    ],
)

envoy_cc_library(
    name = "http_subscription_lib",
    hdrs = ["http_subscription_impl.h"],
    external_deps = ["envoy_base"],
    deps = [
        "//include/envoy/config:subscription_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:rest_api_fetcher_lib",
    ],
)

envoy_cc_library(
    name = "subscription_factory_lib",
    hdrs = ["subscription_factory.h"],
    external_deps = ["envoy_base"],
    deps = [
        ":filesystem_subscription_lib",
        ":grpc_subscription_lib",
        ":http_subscription_lib",
        ":utility_lib",
        "//include/envoy/config:subscription_interface",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    external_deps = ["envoy_base"],
    deps = [
        "//include/envoy/config:subscription_interface",
        "//include/envoy/upstream:cluster_manager_interface",
    ],
)
