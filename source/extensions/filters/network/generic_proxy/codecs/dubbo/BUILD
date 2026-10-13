load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    deps = [
        "//source/extensions/common/dubbo:codec_lib",
        "//source/extensions/filters/network/generic_proxy/interface:codec_interface",
        "@envoy_api//envoy/extensions/filters/network/generic_proxy/codecs/dubbo/v3:pkg_cc_proto",
    ],
)
