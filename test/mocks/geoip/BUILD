load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "dummy",
    srcs = ["dummy.proto"],
)

envoy_cc_mock(
    name = "geoip_mocks",
    hdrs = ["mocks.h"],
    tags = ["skip_on_windows"],
    deps = [
        ":dummy_cc_proto",
        "//envoy/geoip:geoip_provider_driver_interface",
    ],
)
