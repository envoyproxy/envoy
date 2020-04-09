licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "ratelimit_lib",
    srcs = ["ratelimit_impl.cc"],
    hdrs = ["ratelimit_impl.h"],
    deps = [
        ":ratelimit_client_interface",
        "//include/envoy/grpc:async_client_interface",
        "//include/envoy/grpc:async_client_manager_interface",
        "//include/envoy/ratelimit:ratelimit_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/http:headers_lib",
        "//source/common/tracing:http_tracer_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/common/ratelimit/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "ratelimit_client_interface",
    hdrs = ["ratelimit.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/ratelimit:ratelimit_interface",
        "//include/envoy/singleton:manager_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//source/common/stats:symbol_table_lib",
    ],
)

envoy_cc_library(
    name = "stat_names_lib",
    hdrs = ["stat_names.h"],
    deps = [
        "//source/common/stats:symbol_table_lib",
    ],
)
