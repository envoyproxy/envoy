load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "ratelimit_lib",
    srcs = ["ratelimit_impl.cc"],
    hdrs = ["ratelimit_impl.h"],
    deps = [
        ":ratelimit_client_interface",
        "//envoy/grpc:async_client_interface",
        "//envoy/grpc:async_client_manager_interface",
        "//envoy/ratelimit:ratelimit_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/upstream:cluster_manager_interface",
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
    deps = [
        "//envoy/ratelimit:ratelimit_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/tracing:tracer_interface",
        "//source/common/stats:symbol_table_lib",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/service/ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "stat_names_lib",
    hdrs = ["stat_names.h"],
    deps = [
        "//source/common/stats:symbol_table_lib",
    ],
)
