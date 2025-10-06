"""Core extension for Envoy's dependencies and repositories (bzlmod-only).

This extension is for BZLMOD mode only and should never be called from WORKSPACE.
It creates repositories that are NOT available in Bazel Central Registry (BCR).

For WORKSPACE mode, use the functions in //bazel:repositories.bzl instead.

IMPORTANT: This extension calls envoy_dependencies() which is smart enough to skip
repositories that already exist (like those from BCR in bzlmod mode). The 
envoy_http_archive function checks native.existing_rules() before creating repos.
"""

load("//bazel:repositories.bzl", "envoy_dependencies")
load("@com_google_protobuf//bazel/private:proto_bazel_features.bzl", "proto_bazel_features")
load("//bazel/external/cargo:crates.bzl", "raze_fetch_remote_crates")

def _python_minor_version(python_version):
    return "_".join(python_version.split(".")[:-1])

PYTHON_VERSION = "3.12.3"
PYTHON_MINOR_VERSION = _python_minor_version(PYTHON_VERSION)

def _core_impl(module_ctx, python_version = PYTHON_VERSION, ignore_root_user_error = False):
    """Implementation for core extension (bzlmod-only).

    This extension provides repositories not available in BCR by calling
    envoy_dependencies(). In bzlmod mode, this is safe because:
    1. BCR dependencies (rules_cc, protobuf, etc.) already exist from MODULE.bazel
    2. envoy_http_archive checks native.existing_rules() before creating repos
    3. Only non-BCR repositories (aws_lc, grpc, custom deps) are actually created
    
    This is bzlmod-only - do not call from WORKSPACE files.
    """

    # Core dependencies setup - safe in bzlmod because it skips existing repos
    envoy_dependencies()

    # Rust crate dependencies (not in BCR)
    raze_fetch_remote_crates()

    # Proto bazel features (helper for protobuf)
    if not native.existing_rule("proto_bazel_features"):
        proto_bazel_features(name = "proto_bazel_features")

# Consolidated module extension for Envoy core dependencies (bzlmod-only)
core = module_extension(
    implementation = _core_impl,
    doc = """
    Core extension for Envoy's dependencies and repositories in bzlmod mode.
    
    This extension calls envoy_dependencies() which creates 70+ repositories
    not in BCR. It safely coexists with BCR deps because envoy_http_archive
    skips repositories that already exist.
    
    For WORKSPACE mode, call envoy_dependencies() directly from WORKSPACE.
    This extension should never be called from WORKSPACE files.
    
    Features:
    - Comprehensive dependency management
    - Complex patch and configuration handling
    - Rust ecosystem integration
    - Protocol buffer feature configuration
    """,
)