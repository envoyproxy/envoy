load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_library(
    name = "certificates_lib",
    srcs = select({
        "//bazel:exclude_certificates": [],
        "//conditions:default": [
            "certificates.inc",
        ],
    }),
    copts = select({
        "//bazel:exclude_certificates": ["-DEXCLUDE_CERTIFICATES"],
        "//conditions:default": [],
    }),
    repository = "@envoy",
)
