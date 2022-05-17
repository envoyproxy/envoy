load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "config_lib",
    srcs = [
        "certificates.inc",
        "config.cc",
    ],
    hdrs = [
        "internal.h",
        "templates.h",
    ],
    repository = "@envoy",
    deps = [
        "@envoy//source/extensions/http/header_formatters/preserve_case:config",
        "@envoy//source/extensions/http/header_formatters/preserve_case:preserve_case_formatter",
    ],
)
