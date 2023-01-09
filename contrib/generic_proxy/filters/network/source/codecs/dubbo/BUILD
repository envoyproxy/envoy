load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_contrib_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    deps = [
        "//contrib/generic_proxy/filters/network/source/interface:codec_interface",
        "//source/extensions/common/dubbo:codec_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/generic_proxy/codecs/dubbo/v3:pkg_cc_proto",
    ],
)
