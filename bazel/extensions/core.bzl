"""Core extension for Envoy's dependencies and repositories.

This extension provides Envoy's core dependency management, handling
over 100 repository definitions and complex dependency relationships.
"""

load("//bazel:repositories.bzl", "envoy_dependencies")
load("@com_google_protobuf//bazel/private:proto_bazel_features.bzl", "proto_bazel_features")
load("//bazel/external/cargo:crates.bzl", "raze_fetch_remote_crates")

def _python_minor_version(python_version):
    return "_".join(python_version.split(".")[:-1])

PYTHON_VERSION = "3.12.3"
PYTHON_MINOR_VERSION = _python_minor_version(PYTHON_VERSION)

def _core_impl(module_ctx, python_version = PYTHON_VERSION, ignore_root_user_error = False):
    """Implementation for core extension.

    This extension provides Envoy's core dependency setup, handling:
    - Main dependencies and repository definitions
    - Protocol buffer features and setup
    
    Manages 100+ repositories for Envoy's comprehensive dependency ecosystem.
    """

    # Core dependencies setup (from dependencies.bzl)
    envoy_dependencies()

    if not native.existing_rule("proto_bazel_features"):
        proto_bazel_features(name = "proto_bazel_features")

# Consolidated module extension for Envoy core dependencies
core = module_extension(
    implementation = _core_impl,
    doc = """
    Core extension for Envoy's dependencies and repositories.
    
    This extension provides:
    - Main Envoy dependencies with patches and complex setup
    - Additional dependencies and crate repositories
    
    Provides repositories:
    - All repositories from envoy_dependencies() (100+ repos)
    - Rust crate repositories via raze_fetch_remote_crates()
    - proto_bazel_features for protobuf integration
    
    Features:
    - Comprehensive dependency management
    - Complex patch and configuration handling
    - Rust ecosystem integration
    - Protocol buffer feature configuration
    """,
)