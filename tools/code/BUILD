load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_entry_point")
load("@envoy_repo//:path.bzl", "PATH")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_entry_point(
    name = "check",
    args = ["--path=%s" % PATH],
    pkg = "envoy.code.check",
)
