load("@aspect_bazel_lib//lib:repositories.bzl", "aspect_bazel_lib_dependencies")
load("@emsdk//:deps.bzl", emsdk_deps = "deps")
load("@envoy_toolshed//compile:libcxx_libs.bzl", "setup_libcxx_libs")
load("@envoy_toolshed//compile:llvm_minimal.bzl", "llvm_toolchain_alias", "setup_llvm_minimal")
load("@envoy_toolshed//sysroot:sysroot.bzl", "setup_sysroots")
load("@protobuf//bazel/private/oss:proto_bazel_features.bzl", "proto_bazel_features")
load("@proxy-wasm-cpp-host//bazel/cargo/wasmtime/remote:crates.bzl", wasmtime_crate_repositories = "crate_repositories")
load("@rules_cc//cc:extensions.bzl", "compatibility_proxy_repo")
load("@rules_java//java:rules_java_deps.bzl", java_compatibility_proxy_repo = "compatibility_proxy_repo")
load("@rules_python//python:repositories.bzl", "py_repositories", "python_register_toolchains")
load("@toolchains_llvm//toolchain:deps.bzl", "bazel_toolchain_dependencies")
load("//bazel/external/cargo/remote:crates.bzl", wasm_examples_crate_repositories = "crate_repositories")

def _python_minor_version(python_version):
    return "_".join(python_version.split(".")[:-1])

GLIBC_VERSION = "2.31"

# Python version for `rules_python`
PYTHON_VERSION = "3.12.3"
PYTHON_MINOR_VERSION = _python_minor_version(PYTHON_VERSION)

# Envoy deps that rely on a first stage of dependency loading in envoy_dependencies().
def envoy_dependencies_extra(
        glibc_version = GLIBC_VERSION,
        python_version = PYTHON_VERSION,
        ignore_root_user_error = False,
        use_host_tools = False):
    compatibility_proxy_repo()
    java_compatibility_proxy_repo()
    bazel_toolchain_dependencies()
    setup_libcxx_libs()
    if not use_host_tools:
        setup_llvm_minimal()
        llvm_toolchain_alias(
            name = "llvm_toolchain_llvm",
            minimal_linux_x64 = "@llvm_minimal_linux_x64//:BUILD.bazel",
            minimal_linux_arm64 = "@llvm_minimal_linux_arm64//:BUILD.bazel",
            minimal_macos_arm64 = "@llvm_minimal_macos_arm64//:BUILD.bazel",
        )
    setup_sysroots(glibc_version = glibc_version)
    emsdk_deps()
    wasm_examples_crate_repositories()
    wasmtime_crate_repositories()
    py_repositories()

    # Registers underscored Python minor version - eg `python3_10`
    # When use_host_tools is True, skip registering the hermetic Python toolchain
    # and use the host Python instead. This is unsupported by the Envoy project
    # and intended only for downstream builds in controlled environments.
    python_register_toolchains(
        name = "python%s" % _python_minor_version(python_version),
        python_version = python_version,
        ignore_root_user_error = ignore_root_user_error,
        register_toolchains = not use_host_tools,
    )

    if use_host_tools:
        native.register_toolchains("@rules_python//python:autodetecting_toolchain")

    aspect_bazel_lib_dependencies()

    if not native.existing_rule("proto_bazel_features"):
        proto_bazel_features(name = "proto_bazel_features")
