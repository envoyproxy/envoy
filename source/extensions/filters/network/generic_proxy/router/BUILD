load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "router_lib",
    srcs = [
        "router.cc",
        "upstream.cc",
    ],
    hdrs = [
        "router.h",
        "upstream.h",
    ],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:well_known_names",
        "//source/common/router:metadatamatchcriteria_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/tracing:tracer_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/extensions/filters/network/generic_proxy:tracing_lib",
        "//source/extensions/filters/network/generic_proxy/interface:codec_interface",
        "//source/extensions/filters/network/generic_proxy/interface:filter_interface",
        "@com_github_google_quiche//:quiche_common_lib",
        "@envoy_api//envoy/extensions/filters/network/generic_proxy/router/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    deps = [
        ":router_lib",
        "//envoy/registry",
        "//source/extensions/filters/network/generic_proxy/interface:filter_interface",
        "@envoy_api//envoy/extensions/filters/network/generic_proxy/router/v3:pkg_cc_proto",
    ],
)
