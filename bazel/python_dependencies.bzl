load("@com_google_protobuf//bazel:system_python.bzl", "system_python")
load("@envoy_toolshed//:packages.bzl", "load_packages")
load("@python3_12//:defs.bzl", "interpreter")
load("@rules_python//python:pip.bzl", "pip_parse")

def envoy_python_dependencies():
    # TODO(phlax): rename base_pip3 -> pip3 and remove this
    load_packages()
    pip_parse(
        name = "base_pip3",
        python_interpreter_target = interpreter,
        requirements_lock = "@envoy//tools/base:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )

    pip_parse(
        name = "dev_pip3",
        python_interpreter_target = interpreter,
        requirements_lock = "@envoy//tools/dev:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )

    pip_parse(
        name = "fuzzing_pip3",
        python_interpreter_target = interpreter,
        requirements_lock = "@rules_fuzzing//fuzzing:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )

    system_python(
        name = "system_python",
        minimum_python_version = "3.7",
    )
