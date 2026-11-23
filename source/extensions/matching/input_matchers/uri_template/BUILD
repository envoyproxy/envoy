load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "matcher_lib",
    hdrs = ["matcher.h"],
    deps = [
        "//envoy/matcher:matcher_interface",
        "//source/common/http:path_utility_lib",
        "@re2",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":matcher_lib",
        "//envoy/common:exception_lib",
        "//envoy/matcher:matcher_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//source/common/common:thread_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/path/uri_template_lib",
        "@envoy_api//envoy/extensions/matching/input_matchers/uri_template/v3:pkg_cc_proto",
        "@re2",
    ],
)
