load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "id_lib",
    srcs = ["id.cc"],
    hdrs = ["id.h"],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//envoy/event:timer_interface",
        "//envoy/stats:stats_macros",
        "//envoy/server:lifecycle_notifier_interface",
        "//source/common/grpc:common_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:base_logger_lib",
        "//source/common/config:remote_data_fetcher_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@com_google_absl//absl/strings",
        "@com_google_googleapis//google/api/expr/v1alpha1:value_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "attributes_lib",
    srcs = ["attributes.cc"],
    hdrs = ["attributes.h"],
    deps = [
        ":id_lib",
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//envoy/event:timer_interface",
        "//envoy/stats:stats_macros",
        "//envoy/server:lifecycle_notifier_interface",
        "//source/common/grpc:common_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:base_logger_lib",
        "//source/common/config:remote_data_fetcher_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@com_google_absl//absl/strings",
        "@com_google_googleapis//google/api/expr/v1alpha1:value_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3alpha:pkg_cc_proto",
    ],
)