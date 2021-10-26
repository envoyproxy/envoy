load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_package()

envoy_cc_library(
    name = "router_lib",
    srcs = [
        "router.cc",
    ],
    hdrs = [
        "router.h",
    ],
    copts = [
        "-Wno-error=unused-private-field",
    ],
    repository = "@envoy",
    deps = [
        "//contrib/generic_proxy/filters/network/source/interface:generic_codec_interface",
        "//contrib/generic_proxy/filters/network/source/interface:generic_config_interface",
        "//contrib/generic_proxy/filters/network/source/interface:generic_filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/upstream:load_balancer_lib",
    ],
)
