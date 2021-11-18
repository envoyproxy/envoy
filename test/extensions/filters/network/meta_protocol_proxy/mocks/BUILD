load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "stream_filter_mocks",
    srcs = ["stream_filter.cc"],
    hdrs = ["stream_filter.h"],
    deps = [
        "//source/extensions/filters/network/meta_protocol_proxy/interface:config_interface",
        "//source/extensions/filters/network/meta_protocol_proxy/interface:filter_interface",
        "//source/common/protobuf:protobuf",
    ],
)

envoy_cc_mock(
    name = "route_matcher_mocks",
    srcs = ["route_matcher.cc"],
    hdrs = ["route_matcher.h"],
    deps = [
        "//source/common/config:metadata_lib",
        "//source/extensions/filters/network/meta_protocol_proxy/interface:route_interface",
    ],
)

envoy_cc_mock(
    name = "codec_factory_mocks",
    srcs = ["codec_factory.cc"],
    hdrs = ["codec_factory.h"],
    deps = [
        "//source/extensions/filters/network/meta_protocol_proxy/interface:codec_interface",
    ],
)
