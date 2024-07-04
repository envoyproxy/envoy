load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "metadata_lib",
    srcs = ["matcher.cc"],
    hdrs = ["matcher.h"],
    deps = [
        "//envoy/matcher:matcher_interface",
        "//source/common/common:matchers_lib",
        "//source/extensions/matching/http/metadata_input:metadata_input_lib",
        "@envoy_api//envoy/extensions/matching/input_matchers/metadata/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":metadata_lib",
        "//envoy/matcher:matcher_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "@envoy_api//envoy/extensions/matching/input_matchers/metadata/v3:pkg_cc_proto",
    ],
)
