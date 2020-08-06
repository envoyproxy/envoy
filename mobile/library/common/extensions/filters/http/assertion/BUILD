load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library")
load("@envoy_api//bazel:api_build_system.bzl", "api_proto_package")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

api_proto_package(
    deps = ["@envoy_api//envoy/config/tap/v3:pkg"],
)

envoy_cc_library(
    name = "assertion_filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    repository = "@envoy",
    deps = [
        ":pkg_cc_proto",
        "@envoy//include/envoy/http:codes_interface",
        "@envoy//include/envoy/http:filter_interface",
        "@envoy//source/extensions/common/tap:tap_matcher",
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":assertion_filter_lib",
        ":pkg_cc_proto",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
    ],
)
