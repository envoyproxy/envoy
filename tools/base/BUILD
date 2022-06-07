load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_entry_point")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "entry_point.py",
])

envoy_entry_point(
    name = "parallel",
    pkg = "envoy.base.utils",
    script = "envoy.parallel",
)
