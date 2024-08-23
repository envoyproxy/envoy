load("@rules_python//python:pip.bzl", "compile_pip_requirements")

licenses(["notice"])  # Apache 2

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
