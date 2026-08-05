load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "health_checker_lib",
    srcs = ["multi.cc"],
    hdrs = ["multi.h"],
    extra_visibility = [
        "//test:__subpackages__",
    ],
    deps = [
        "//envoy/registry",
        "//source/common/common:callback_impl_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:health_checker_lib",
        "@envoy_api//envoy/extensions/health_checkers/multi/v3:pkg_cc_proto",
    ],
)
