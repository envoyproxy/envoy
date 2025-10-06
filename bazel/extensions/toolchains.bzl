"""Toolchains extension for Envoy's toolchain management and imports (bzlmod-only).

This extension is for BZLMOD mode only and should never be called from WORKSPACE.
It provides toolchain configuration and imports not available in Bazel Central Registry.

For WORKSPACE mode, use the functions in //bazel:repositories.bzl and 
//bazel:repositories_extra.bzl instead.
"""

load("@envoy_toolshed//compile:sanitizer_libs.bzl", "setup_sanitizer_libs")
load("@envoy_toolshed//coverage/grcov:grcov_repository.bzl", "grcov_repository")
load("@rules_fuzzing//fuzzing:repositories.bzl", "rules_fuzzing_dependencies")
load("@com_google_cel_cpp//bazel:deps.bzl", "parser_deps")
load("//bazel:repo.bzl", "envoy_repo")

def _toolchains_impl(module_ctx):
    """Implementation for toolchains extension (bzlmod-only).

    This extension provides Envoy's toolchain and import setup:
    - Dependency imports and toolchain registration
    - Foreign CC and build tool configuration  
    - Repository metadata and environment setup
    
    Manages complex toolchain ecosystem for Envoy's build environment.
    """

    # Main dependency imports setup
    grcov_repository()
    setup_sanitizer_libs()
    
    # Try to conditionally call rules_fuzzing_dependencies only if needed
    if not native.existing_rule("rules_fuzzing_oss_fuzz"):
        rules_fuzzing_dependencies(
            oss_fuzz = True,
            honggfuzz = False,
        )
    
    parser_deps()
    
    # Repository metadata setup
    envoy_repo()

# Module extension for Envoy toolchains and imports (bzlmod-only)
toolchains = module_extension(
    implementation = _toolchains_impl,
    doc = """
    Extension for Envoy's toolchains and imports (bzlmod-only).
    
    This extension provides toolchain setup not in BCR.
    For WORKSPACE mode, use //bazel:repositories.bzl functions instead.
    This extension should never be called from WORKSPACE files.
    
    Handles:
    - Sanitizer libraries and grcov setup
    - Fuzzing dependencies and toolchains  
    - Parser dependencies (CEL)
    - Repository metadata and tooling configuration
    """,
)