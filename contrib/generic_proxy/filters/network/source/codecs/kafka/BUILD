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
        "//contrib/kafka/filters/network/source:kafka_request_codec_lib",
        "//contrib/kafka/filters/network/source:kafka_response_codec_lib",
        "//source/extensions/filters/network/generic_proxy/interface:codec_interface",
        "@envoy_api//contrib/envoy/extensions/filters/network/generic_proxy/codecs/kafka/v3:pkg_cc_proto",
    ],
)
