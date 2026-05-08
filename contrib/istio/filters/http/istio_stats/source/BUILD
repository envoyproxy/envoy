load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_contrib_extension(
    name = "istio_stats",
    srcs = ["istio_stats.cc"],
    hdrs = ["istio_stats.h"],
    deps = [
        "//contrib/istio/filters/common/source:metadata_object_lib",
        "//envoy/registry",
        "//envoy/router:string_accessor_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/grpc:common_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/network:utility_lib",
        "//source/common/stream_info:utility_lib",
        "//source/extensions/filters/common/expr:context_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//source/extensions/filters/http/grpc_stats:config",
        "@cel-cpp//eval/public:builtin_func_registrar",
        "@cel-cpp//eval/public:cel_expr_builder_factory",
        "@cel-cpp//parser",
        "@envoy_api//contrib/envoy/extensions/filters/http/istio_stats/v3:pkg_cc_proto",
    ],
)
