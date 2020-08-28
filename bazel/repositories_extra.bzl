load("@rules_python//python:repositories.bzl", "py_repositories")
load("@rules_python//python:pip.bzl", "pip3_import", "pip_repositories")

# Python dependencies.
def _python_deps():
    py_repositories()
    pip_repositories()

    pip3_import(
        name = "config_validation_pip3",
        requirements = "@envoy//tools/config_validation:requirements.txt",
    )
    pip3_import(
        name = "configs_pip3",
        requirements = "@envoy//configs:requirements.txt",
    )
    pip3_import(
<<<<<<< HEAD
        name = "kafka_pip3",
        requirements = "@envoy//source/extensions/filters/network/kafka:requirements.txt",
=======
        name = "protodoc_pip3",
        requirements = "@envoy//tools/protodoc:requirements.txt",
>>>>>>> upstream/master
    )
    pip3_import(
        name = "headersplit_pip3",
        requirements = "@envoy//tools/envoy_headersplit:requirements.txt",
    )
    pip3_import(
        name = "protodoc_pip3",
        requirements = "@envoy//tools/protodoc:requirements.txt",
    )

# Envoy deps that rely on a first stage of dependency loading in envoy_dependencies().
def envoy_dependencies_extra():
    _python_deps()
