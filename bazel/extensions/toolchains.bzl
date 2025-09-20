"""Toolchains extension for Envoy's toolchain management and imports.

This extension provides Envoy's toolchain configuration, dependency imports,
and repository metadata setup.
"""

load("@build_bazel_rules_apple//apple:repositories.bzl", "apple_rules_dependencies")
load("@com_google_cel_cpp//bazel:deps.bzl", "parser_deps")
load("@envoy_toolshed//compile:sanitizer_libs.bzl", "setup_sanitizer_libs")
load("@envoy_toolshed//coverage/grcov:grcov_repository.bzl", "grcov_repository")
load("@rules_fuzzing//fuzzing:repositories.bzl", "rules_fuzzing_dependencies")
load("//bazel:repo.bzl", "envoy_repo")

def _toolchains_impl(module_ctx):
    """Implementation for toolchains extension.

    This extension provides Envoy's toolchain and import setup:
    - Dependency imports and toolchain registration
    - Foreign CC and build tool configuration
    - Repository metadata and environment setup

    Manages complex toolchain ecosystem for Envoy's build environment.
    """

    # Main dependency imports setup
    # In bzlmod mode, since @platforms is a direct dependency, we need to
    # avoid creating duplicate repositories. Try to call only necessary functions.
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

# Module extension for Envoy toolchains and imports
toolchains = module_extension(
    implementation = _toolchains_impl,
    doc = """
    Extension for Envoy's toolchains and imports.
    
    This extension provides:
    - Main toolchain imports and registrations
    - Additional dependency imports
    - Repository metadata and tooling setup
    
    Handles:
    - Go toolchain registration and dependencies
    - Python pip dependencies (base, dev, fuzzing)
    - Rust toolchain and crate universe setup
    - Foreign CC dependencies and toolchains
    - Apple, shellcheck, and other development toolchains
    - Repository metadata and tooling configuration
    
    Features:
    - Comprehensive toolchain management
    - Multi-language support (Go, Python, Rust, C++)
    - Development and testing tool integration
    - Cross-platform build support
    """,
)
