licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":abac_filter_lib",
        "//include/envoy/registry",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@com_google_cel_cpp//eval/public:builtin_func_registrar",
        "@com_google_cel_cpp//eval/public:cel_expr_builder_factory",
    ],
)

envoy_cc_library(
    name = "abac_filter_lib",
    srcs = ["abac_filter.cc"],
    hdrs = ["abac_filter.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        #"//include/envoy/stats:stats_macros",
        "//source/common/http:utility_lib",
        "//source/common/protobuf",
        "@com_google_cel_cpp//eval/public:cel_expression",
        "@envoy_api//envoy/config/filter/http/abac/v2alpha:abac_cc",
    ],
)
