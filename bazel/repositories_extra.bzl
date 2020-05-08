load("@rules_python//python:repositories.bzl", "py_repositories")
load("@rules_python//python:pip.bzl", "pip3_import", "pip_repositories")

# Python dependencies.
def _python_deps():
    py_repositories()
    pip_repositories()

    pip3_import(
        name = "config_validation",
        requirements = "@envoy//tools/config_validation:requirements.txt",
    )

# Envoy deps that rely on a first stage of dependency loading in envoy_dependencies().
def envoy_dependencies_extra():
    _python_deps()
