load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_contrib_extension(
    name = "tra_lib",
    srcs = ["tra_impl.cc"],
    hdrs = ["tra_impl.h"],
    deps = [
        ":tra_client_interface",
        "//envoy/grpc:async_client_interface",
        "//envoy/grpc:async_client_manager_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf",
        "//source/common/tracing:http_tracer_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/sip_proxy/tra/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "tra_client_interface",
    hdrs = ["tra.h"],
    deps = [
        "//envoy/singleton:manager_interface",
        "//envoy/tracing:tracer_interface",
        "//source/common/stats:symbol_table_lib",
        "@com_google_absl//absl/types:any",
        "@com_google_absl//absl/types:optional",
    ],
)
