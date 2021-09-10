load("@rules_python//python:pip.bzl", "pip_install", "pip_parse")
load("@proxy_wasm_cpp_host//bazel/cargo:crates.bzl", "proxy_wasm_cpp_host_fetch_remote_crates")
load("//bazel/external/cargo:crates.bzl", "raze_fetch_remote_crates")

# Python dependencies.
def _python_deps():
    pip_parse(
        name = "base_pip3",
        requirements_lock = "@envoy//tools/base:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )
    pip_install(
        name = "configs_pip3",
        requirements = "@envoy//configs:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )
    pip_install(
        name = "thrift_pip3",
        requirements = "@envoy//test/extensions/filters/network/thrift_proxy:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )
    pip_install(
        name = "fuzzing_pip3",
        requirements = "@rules_fuzzing//fuzzing:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )

# Envoy deps that rely on a first stage of dependency loading in envoy_dependencies().
def envoy_dependencies_extra():
    _python_deps()
    proxy_wasm_cpp_host_fetch_remote_crates()
    raze_fetch_remote_crates()
