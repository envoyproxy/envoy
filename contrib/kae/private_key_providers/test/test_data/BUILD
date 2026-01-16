load(
    "//bazel:envoy_build_system.bzl",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

filegroup(
    name = "certs",
    srcs = glob(["*.pem"]),
)
