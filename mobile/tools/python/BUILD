load("@rules_python//python:pip.bzl", "compile_pip_requirements")

licenses(["notice"])  # Apache 2

exports_files(glob(["**"]))

compile_pip_requirements(
    name = "requirements",
    src = "requirements.in",
    extra_args = [
        "--allow-unsafe",
        "--generate-hashes",
        "--reuse-hashes",
        "--resolver=backtracking",
    ],
)
