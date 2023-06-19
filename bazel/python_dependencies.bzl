load("@rules_python//python:pip.bzl", "pip_parse")
load("@python3_11//:defs.bzl", "interpreter")

def envoy_python_dependencies():
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
