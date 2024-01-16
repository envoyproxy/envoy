load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

# Golang extensions filter.
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/golang_filter

envoy_contrib_package()

envoy_cc_library(
    name = "golang_filter_lib",
    srcs = [
        "golang_filter.cc",
        "processor_state.cc",
    ],
    hdrs = [
        "golang_filter.h",
        "processor_state.h",
        "stats.h",
    ],
    deps = [
        ":cgo",
        "//contrib/golang/common/dso:dso_lib",
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:linked_object",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
        "//source/common/grpc:context_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/expr:cel_state_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "@com_google_cel_cpp//eval/public:activation",
        "@com_google_cel_cpp//eval/public:builtin_func_registrar",
        "@com_google_cel_cpp//eval/public:cel_expr_builder_factory",
        "@com_google_cel_cpp//eval/public:cel_value",
        "@com_google_cel_cpp//eval/public:value_export_util",
        "@com_google_cel_cpp//eval/public/containers:field_access",
        "@com_google_cel_cpp//eval/public/containers:field_backed_list_impl",
        "@com_google_cel_cpp//eval/public/containers:field_backed_map_impl",
        "@com_google_cel_cpp//eval/public/structs:cel_proto_wrapper",
        "@envoy_api//contrib/envoy/extensions/filters/http/golang/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":golang_filter_lib",
        "//contrib/golang/common/dso:dso_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//envoy/server:lifecycle_notifier_interface",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/golang/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "cgo",
    srcs = ["cgo.cc"],
    hdrs = [
        "golang_filter.h",
        "processor_state.h",
        "stats.h",
    ],
    deps = [
        "//contrib/golang/common/dso:dso_lib",
        "//contrib/golang/common/log:log_lib",
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:linked_object",
        "//source/common/common:utility_lib",
        "//source/common/grpc:context_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/expr:cel_state_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/golang/v3alpha:pkg_cc_proto",
    ],
)
