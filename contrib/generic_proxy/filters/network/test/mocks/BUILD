load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_mock(
    name = "filter_mocks",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        "//contrib/generic_proxy/filters/network/source/interface:config_interface",
        "//contrib/generic_proxy/filters/network/source/interface:filter_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_mock(
    name = "route_mocks",
    srcs = ["route.cc"],
    hdrs = ["route.h"],
    deps = [
        "//contrib/generic_proxy/filters/network/source/interface:route_interface",
        "//source/common/config:metadata_lib",
    ],
)

envoy_cc_mock(
    name = "codec_mocks",
    srcs = ["codec.cc"],
    hdrs = ["codec.h"],
    deps = [
        "//contrib/generic_proxy/filters/network/source/interface:codec_interface",
    ],
)
