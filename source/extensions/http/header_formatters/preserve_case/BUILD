load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "preserve_case_formatter",
    srcs = ["preserve_case_formatter.cc"],
    hdrs = ["preserve_case_formatter.h"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/http/http1:header_formatter_lib",
        "@envoy_api//envoy/extensions/http/header_formatters/preserve_case/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":preserve_case_formatter",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/http/header_formatters/preserve_case/v3:pkg_cc_proto",
    ],
)
