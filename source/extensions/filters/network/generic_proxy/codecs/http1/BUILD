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
        "//source/common/http:codes_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/http1:balsa_parser_lib",
        "//source/extensions/filters/network/generic_proxy/interface:codec_interface",
        "@envoy_api//envoy/extensions/filters/network/generic_proxy/codecs/http1/v3:pkg_cc_proto",
    ],
)
