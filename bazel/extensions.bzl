"""Module extensions for Envoy dependencies not available via bazel_dep.

These extensions wrap the repository-loading functions from Envoy's WORKSPACE-based
build system. When bzlmod is enabled, MODULE.bazel uses these extensions instead of
the WORKSPACE file's sequential loading chain.

The envoy_toolchains extension lives in toolchain_extensions.bzl because
toolchains.bzl has a top-level load from @envoy_repo, which is created by
envoy_repo_ext below. Separating them ensures correct load ordering.
"""

load("//bazel:repo.bzl", "envoy_repo")
load("//bazel:repositories.bzl", "envoy_dependencies")

def _non_module_deps_impl(module_ctx):
    """Implementation for non_module_deps extension.

    Calls envoy_dependencies(bzlmod=True) which creates repositories not
    available in BCR or the toolshed registry. It safely coexists with
    bazel_dep entries because envoy_http_archive checks native.existing_rules()
    before creating repositories.

    Args:
        module_ctx: Module extension context
    """
    envoy_dependencies(bzlmod = True)

non_module_deps = module_extension(
    implementation = _non_module_deps_impl,
    doc = """
    Extension for Envoy dependencies not available via bazel_dep.

    This extension creates HTTP archive repositories for Envoy's C++ dependencies
    (BoringSSL, c-ares, gRPC, protobuf, LLVM toolchain, etc.) that are not yet
    available in BCR or the toolshed Bazel registry.

    As dependencies are added to the registries, they should be migrated from this
    extension to bazel_dep entries in MODULE.bazel.

    For WORKSPACE mode, call envoy_dependencies() directly from WORKSPACE.
    This extension should only be used in MODULE.bazel files.
    """,
)

def _envoy_repo_impl(module_ctx):
    """Implementation for envoy_repo_ext extension.

    Creates the @envoy_repo repository which provides version metadata,
    compiler configuration, and container image references used throughout
    the build.

    Args:
        module_ctx: Module extension context
    """
    envoy_repo()

envoy_repo_ext = module_extension(
    implementation = _envoy_repo_impl,
    doc = """
    Extension for the @envoy_repo metadata repository.

    This extension creates the @envoy_repo repository which exposes:
    - version.bzl: VERSION and API_VERSION constants
    - compiler.bzl: LLVM_PATH, USE_LOCAL_SYSROOT, etc.
    - containers.bzl: CI container image references
    - path.bzl: local repository path

    For WORKSPACE mode, call envoy_repo() directly from WORKSPACE.
    This extension should only be used in MODULE.bazel files.
    """,
)
