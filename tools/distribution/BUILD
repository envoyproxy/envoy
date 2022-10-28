load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_entry_point")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_entry_point(
    name = "release",
    pkg = "envoy.distribution.release",
)

envoy_entry_point(
    name = "sign",
    pkg = "envoy.gpg.sign",
)

envoy_entry_point(
    name = "verify",
    pkg = "envoy.distribution.verify",
)
