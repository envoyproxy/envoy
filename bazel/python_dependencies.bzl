load("@rules_python//python:pip.bzl", "pip_install", "pip_parse")
load("@python3_10//:defs.bzl", "interpreter")

def envoy_python_dependencies():
    pip_parse(
        name = "base_pip3",
        python_interpreter_target = interpreter,
        requirements_lock = "@envoy//tools/base:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )

    # TODO(phlax): switch to `pip_parse`
    pip_install(
        # Note: dev requirements do *not* check hashes
        python_interpreter_target = interpreter,
        name = "dev_pip3",
        requirements = "@envoy//tools/dev:requirements.txt",
    )

    pip_parse(
        name = "fuzzing_pip3",
        python_interpreter_target = interpreter,
        requirements_lock = "@rules_fuzzing//fuzzing:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )
