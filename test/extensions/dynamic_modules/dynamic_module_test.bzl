"""Bazel macro for tests that load dynamic modules."""

load("//bazel:envoy_build_system.bzl", "envoy_cc_test")

def envoy_cc_dynamic_module_test(name, **kwargs):
    """Defines an envoy_cc_test whose binary hosts dynamic modules.

    A loaded module resolves the host callbacks against the test binary, so the binary must export
    them. On Linux and macOS this happens through the global exported-symbols lists. Windows has no
    wildcard export and rejects a module-definition file that names an undefined symbol, so the
    export list cannot be applied to every test. This macro passes a module-definition file on
    Windows so only the tests that host dynamic modules export the callbacks.
    """
    envoy_cc_test(
        name = name,
        win_def_file = "//source/extensions/dynamic_modules:windows_exports_def",
        **kwargs
    )
