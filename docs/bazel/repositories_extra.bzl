load("@envoy-examples//bazel:env.bzl", "envoy_examples_env")
load("@rules_python//python:pip.bzl", "pip_parse")

def envoy_docs_repositories_extra():
    pip_parse(
        name = "docs_pip3",
        python_interpreter_target = "@python3_12_host//:python",
        requirements_lock = Label("//tools/python:requirements.txt"),
    )
    envoy_examples_env()
