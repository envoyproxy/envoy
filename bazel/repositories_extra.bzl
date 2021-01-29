load("@rules_python//python:repositories.bzl", "py_repositories")
load("@rules_python//python:pip.bzl", "pip3_import", "pip_repositories")
load("@proxy_wasm_cpp_host//bazel/cargo:crates.bzl", "proxy_wasm_cpp_host_fetch_remote_crates")

# Python dependencies.
def _python_deps():
    py_repositories()
    pip_repositories()

    pip3_import(
        name = "config_validation_pip3",
        requirements = "@envoy//tools/config_validation:requirements.txt",
        extra_pip_args = ["--require-hashes"],

        # project_name = "PyYAML",
        # project_url = "https://github.com/yaml/pyyaml",
        # version = "5.3.1",
        # release_date = "2020-03-18"
        # use_category = ["devtools"],
        # cpe = "cpe:2.3:a:pyyaml:pyyaml:*",
    )
    pip3_import(
        name = "configs_pip3",
        requirements = "@envoy//configs:requirements.txt",
        extra_pip_args = ["--require-hashes"],

        # project_name = "Jinja",
        # project_url = "http://palletsprojects.com/p/jinja",
        # version = "2.11.2",
        # release_date = "2020-04-13"
        # use_category = ["test"],
        # cpe = "cpe:2.3:a:palletsprojects:jinja:*",

        # project_name = "MarkupSafe",
        # project_url = "https://markupsafe.palletsprojects.com/en/1.1.x/",
        # version = "1.1.1",
        # release_date = "2019-02-23"
        # use_category = ["test"],
    )
    pip3_import(
        name = "kafka_pip3",
        requirements = "@envoy//source/extensions/filters/network/kafka:requirements.txt",
        extra_pip_args = ["--require-hashes"],

        # project_name = "Jinja",
        # project_url = "http://palletsprojects.com/p/jinja",
        # version = "2.11.2",
        # release_date = "2020-04-13"
        # use_category = ["test"],
        # cpe = "cpe:2.3:a:palletsprojects:jinja:*",

        # project_name = "MarkupSafe",
        # project_url = "https://markupsafe.palletsprojects.com/en/1.1.x/",
        # version = "1.1.1",
        # release_date = "2019-02-23"
        # use_category = ["test"],
    )
    pip3_import(
        name = "headersplit_pip3",
        requirements = "@envoy//tools/envoy_headersplit:requirements.txt",
        extra_pip_args = ["--require-hashes"],

        # project_name = "Clang",
        # project_url = "https://clang.llvm.org/",
        # version = "10.0.1",
        # release_date = "2020-07-21"
        # use_category = ["devtools"],
        # cpe = "cpe:2.3:a:llvm:clang:*",
    )
    pip3_import(
        name = "protodoc_pip3",
        requirements = "@envoy//tools/protodoc:requirements.txt",
        extra_pip_args = ["--require-hashes"],

        # project_name = "PyYAML",
        # project_url = "https://github.com/yaml/pyyaml",
        # version = "5.3.1",
        # release_date = "2020-03-18"
        # use_category = ["docs"],
        # cpe = "cpe:2.3:a:pyyaml:pyyaml:*",
    )
    pip3_import(
        name = "thrift_pip3",
        requirements = "@envoy//test/extensions/filters/network/thrift_proxy:requirements.txt",
        extra_pip_args = ["--require-hashes"],

        # project_name = "Apache Thrift",
        # project_url = "http://thrift.apache.org/",
        # version = "0.11.0",
        # release_date = "2017-12-07"
        # use_category = ["test"],
        # cpe = "cpe:2.3:a:apache:thrift:*",

        # project_name = "Six: Python 2 and 3 Compatibility Library",
        # project_url = "https://six.readthedocs.io/",
        # version = "1.15.0",
        # release_date = "2020-05-21"
        # use_category = ["test"],
    )
    pip3_import(
        name = "fuzzing_pip3",
        requirements = "@rules_fuzzing//fuzzing:requirements.txt",
        extra_pip_args = ["--require-hashes"],

        # project_name = "Abseil Python Common Libraries",
        # project_url = "https://github.com/abseil/abseil-py",
        # version = "0.11.0",
        # release_date = "2020-10-27",
        # use_category = ["test"],

        # project_name = "Six: Python 2 and 3 Compatibility Library",
        # project_url = "https://six.readthedocs.io/",
        # version = "1.15.0",
        # release_date = "2020-05-21"
        # use_category = ["test"],
    )

# Envoy deps that rely on a first stage of dependency loading in envoy_dependencies().
def envoy_dependencies_extra():
    _python_deps()
    proxy_wasm_cpp_host_fetch_remote_crates()
