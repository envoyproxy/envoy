load("@rules_python//python:repositories.bzl", "py_repositories")
load("@rules_python//python:pip.bzl", "pip3_import", "pip_repositories")

# Python dependencies.
def _python_deps():
    py_repositories()
    pip_repositories()

    pip3_import(
        # project_name = "PyYAML",
        # project_url = "https://github.com/yaml/pyyaml",
        # version = "5.3.1",
        # use_category = ["other"],
        # cpe = "cpe:2.3:a:pyyaml:pyyaml:*",
        name = "config_validation_pip3",
        requirements = "@envoy//tools/config_validation:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )
    pip3_import(
        # project_name = "Jinja",
        # project_url = "http://palletsprojects.com/p/jinja",
        # version = "2.11.2",
        # use_category = ["test"],
        # cpe = "cpe:2.3:a:palletsprojects:jinja:*",
        name = "configs_pip3",
        requirements = "@envoy//configs:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )
    pip3_import(
        # project_name = "Jinja",
        # project_url = "http://palletsprojects.com/p/jinja",
        # version = "2.11.2",
        # use_category = ["test"],
        # cpe = "cpe:2.3:a:palletsprojects:jinja:*",
        name = "kafka_pip3",
        requirements = "@envoy//source/extensions/filters/network/kafka:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )
    pip3_import(
        name = "headersplit_pip3",
        requirements = "@envoy//tools/envoy_headersplit:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )
    pip3_import(
        # project_name = "PyYAML",
        # project_url = "https://github.com/yaml/pyyaml",
        # version = "5.3.1",
        # use_category = ["other"],
        # cpe = "cpe:2.3:a:pyyaml:pyyaml:*",
        name = "protodoc_pip3",
        requirements = "@envoy//tools/protodoc:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )
    pip3_import(
        # project_name = "Apache Thrift",
        # project_url = "http://thrift.apache.org/",
        # version = "0.11.0",
        # use_category = ["dataplane"],
        # cpe = "cpe:2.3:a:apache:thrift:*",
        name = "thrift_pip3",
        requirements = "@envoy//test/extensions/filters/network/thrift_proxy:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )

# Envoy deps that rely on a first stage of dependency loading in envoy_dependencies().
def envoy_dependencies_extra():
    _python_deps()
