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
    hdrs = ["config.h"],
    deps = [
        ":connection_balancer",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//source/common/network:connection_balancer_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//contrib/envoy/extensions/network/connection_balance/random/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "connection_balancer",
    srcs = [
        "connection_balancer_impl.cc",
    ],
    hdrs = ["connection_balancer_impl.h"],
    deps = [
        "//source/common/network:connection_balancer_lib",
        "//source/common/protobuf:utility_lib",
    ],
)
