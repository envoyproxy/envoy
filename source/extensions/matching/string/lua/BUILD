load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["match.cc"],
    deps = [
        "//source/common/common:matchers_lib",
        "@envoy_api//envoy/extensions/matching/string/lua/v3:pkg_cc_proto",
    ],
)
