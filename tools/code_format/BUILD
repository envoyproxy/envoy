load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//bazel:envoy_go.bzl", "gen_gofmt")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "check_format.py",
    "header_order.py",
    "envoy_build_fixer.py",
])

gen_gofmt(
    name = "gofmt",
)
