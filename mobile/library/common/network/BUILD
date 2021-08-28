load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "mobile_utility_lib",
    srcs = ["mobile_utility.cc"],
    hdrs = ["mobile_utility.h"],
    copts = ["-Ithird_party/android"],
    repository = "@envoy",
    deps = [
        "@envoy//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "synthetic_address_lib",
    hdrs = ["synthetic_address_impl.h"],
    repository = "@envoy",
    deps = [
        "@envoy//envoy/network:address_interface",
        "@envoy//source/common/network:socket_interface_lib",
    ],
)
