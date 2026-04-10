load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "orca_parser",
    srcs = ["orca_parser.cc"],
    hdrs = ["orca_parser.h"],
    deps = [
        "//envoy/common:exception_lib",
        "//envoy/http:header_map_interface",
        "//source/common/common:base64_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/protobuf:utility_lib_header",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@fmt",
        "@xds//xds/data/orca/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "orca_oob_session_lib",
    srcs = ["orca_oob_session.cc"],
    hdrs = ["orca_oob_session.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/common:random_generator_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/grpc:status",
        "//envoy/http:codec_interface",
        "//envoy/network:connection_interface",
        "//envoy/stats:stats_macros",
        "//envoy/upstream:upstream_interface",
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/common:backoff_lib",
        "//source/common/common:logger_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/grpc:status_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:response_decoder_impl_base",
        "//source/common/http:utility_lib",
        "//source/common/protobuf",
        "//source/common/router:router_lib",
        "@xds//xds/data/orca/v3:pkg_cc_proto",
        "@xds//xds/service/orca/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "orca_load_metrics_lib",
    srcs = ["orca_load_metrics.cc"],
    hdrs = ["orca_load_metrics.h"],
    deps = [
        ":orca_parser",
        "//envoy/http:header_map_interface",
        "//source/common/http:header_utility_lib",
        "//source/common/protobuf:utility_lib_header",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@fmt",
        "@xds//xds/data/orca/v3:pkg_cc_proto",
    ],
)
