load("@rules_python//python:pip.bzl", "pip_install")
load("@proxy_wasm_cpp_host//bazel/cargo:crates.bzl", "proxy_wasm_cpp_host_fetch_remote_crates")
load("//bazel/external/cargo:crates.bzl", "raze_fetch_remote_crates")

# Python dependencies.
def _python_deps():
    pip_install(
        name = "base_pip3",
        requirements = "@envoy//tools/base:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )
    pip_install(
        name = "docs_pip3",
        requirements = "@envoy//tools/docs:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )
    pip_install(
        name = "distribution_pip3",
        requirements = "@envoy//tools/distribution:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )
    pip_install(
        name = "pylint_pip3",
        requirements = "@envoy//tools/code_format:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )
    pip_install(
        name = "testing_pip3",
        requirements = "@envoy//tools/testing:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )
    pip_install(
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
    pip_install(
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
    raze_fetch_remote_crates()
