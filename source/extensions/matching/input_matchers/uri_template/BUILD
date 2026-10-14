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
        "//envoy/router:path_matcher_interface",
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
        "//source/extensions/path/match/uri_template:config",
        "@envoy_api//envoy/extensions/path/match/uri_template/v3:pkg_cc_proto",
    ],
)
