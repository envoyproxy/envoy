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
    hdrs = ["match.h"],
    deps = [
        "//source/common/common:matchers_lib",
        "//source/common/config:datasource_lib",
        "//source/extensions/filters/common/lua:lua_lib",
        "@envoy_api//envoy/extensions/string_matcher/lua/v3:pkg_cc_proto",
    ],
)
