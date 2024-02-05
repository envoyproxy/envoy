load("@rules_python//python:pip.bzl", "compile_pip_requirements")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "entry_point.py",
])

compile_pip_requirements(
    name = "requirements",
    extra_args = [
        "--allow-unsafe",
        "--generate-hashes",
        "--reuse-hashes",
        "--resolver=backtracking",
    ],
)
