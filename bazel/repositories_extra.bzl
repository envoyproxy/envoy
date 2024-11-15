load("@aspect_bazel_lib//lib:repositories.bzl", "aspect_bazel_lib_dependencies")
load("@bazel_features//:deps.bzl", "bazel_features_deps")
load("@emsdk//:deps.bzl", emsdk_deps = "deps")
load("@proxy_wasm_cpp_host//bazel/cargo/wasmtime/remote:crates.bzl", "crate_repositories")
load("@rules_python//python:repositories.bzl", "py_repositories", "python_register_toolchains")
load("//bazel/external/cargo:crates.bzl", "raze_fetch_remote_crates")

def _python_minor_version(python_version):
    return "_".join(python_version.split(".")[:-1])

# Python version for `rules_python`
PYTHON_VERSION = "3.12.3"
PYTHON_MINOR_VERSION = _python_minor_version(PYTHON_VERSION)

# Envoy deps that rely on a first stage of dependency loading in envoy_dependencies().
def envoy_dependencies_extra(
        python_version = PYTHON_VERSION,
        ignore_root_user_error = False):
    bazel_features_deps()
    emsdk_deps()
    raze_fetch_remote_crates()
    crate_repositories()
    py_repositories()

    # Registers underscored Python minor version - eg `python3_10`
    python_register_toolchains(
        name = "python%s" % _python_minor_version(python_version),
        python_version = python_version,
        ignore_root_user_error = ignore_root_user_error,
    )

    aspect_bazel_lib_dependencies()
