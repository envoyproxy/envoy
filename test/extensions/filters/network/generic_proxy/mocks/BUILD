load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "filter_mocks",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    rbe_pool = "2core",
    deps = [
        "//source/common/protobuf",
        "//source/extensions/filters/network/generic_proxy/interface:filter_interface",
        "//test/mocks/network:network_mocks",
        "//test/mocks/tcp:tcp_mocks",
        "//test/mocks/upstream:host_mocks",
    ],
)

envoy_cc_mock(
    name = "route_mocks",
    srcs = ["route.cc"],
    hdrs = ["route.h"],
    deps = [
        "//source/common/config:metadata_lib",
        "//source/extensions/filters/network/generic_proxy:route_interface",
    ],
)

envoy_cc_mock(
    name = "codec_mocks",
    srcs = ["codec.cc"],
    hdrs = ["codec.h"],
    deps = [
        "//source/extensions/filters/network/generic_proxy/interface:codec_interface",
    ],
)
