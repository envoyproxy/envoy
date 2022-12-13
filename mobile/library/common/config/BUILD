load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "config_lib",
    srcs = [
        "config.cc",
    ] + select({
        "//bazel:exclude_certificates": [
            "certificates.inc",
        ],
        "//conditions:default": [],
    }),
    hdrs = [
        "internal.h",
        "templates.h",
    ],
    copts = select({
        "//bazel:exclude_certificates": ["-DEXCLUDE_CERTIFICATES"],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    deps = [
        "@envoy//source/extensions/http/header_formatters/preserve_case:config",
        "@envoy//source/extensions/http/header_formatters/preserve_case:preserve_case_formatter",
    ],
)
