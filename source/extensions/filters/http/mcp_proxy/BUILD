load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)
 
licenses(["notice"])  # Apache 2
 
envoy_extension_package()
 
envoy_cc_library(
    name = "mcp_proxy_filter_lib",
    srcs = [
        "mcp_filter.cc",
    ],
    hdrs = [
        "mcp_filter.h",
    ],
    deps = [
        "@envoy//source/common/buffer:buffer_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:utility_lib",
        "@envoy//source/common/http:headers_lib",
        "//envoy/http:mcp_handler_interface",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@envoy_api//envoy/extensions/filters/http/mcp_proxy/v3:pkg_cc_proto",
        "@com_github_nlohmann_json//:json",
    ],
    visibility = ["//visibility:public"],
)
 
envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//envoy/http:mcp_handler_interface",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/mcp_proxy:mcp_proxy_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/mcp_proxy/v3:pkg_cc_proto",
    ],
)
