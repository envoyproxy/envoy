load("@envoy_build_config//:extensions_build_config.bzl", "EXTENSIONS")
load("//bazel:envoy_build_system.bzl", "envoy_benchmark_test", "envoy_cc_benchmark_binary", "envoy_cc_mock", "envoy_cc_test", "envoy_cc_test_binary", "envoy_cc_test_library", "envoy_py_test")

def _apply_to_extension_allow_for_test(func, name, extension_names, **kwargs):
    for extension_name in extension_names:
        if not extension_name in EXTENSIONS:
            return

    func(name, **kwargs)

# All extension tests should use this version of envoy_cc_test(). It allows compiling out
# tests for extensions that the user does not wish to include in their build.
# @param each item of extension_names should match an extension listed in EXTENSIONS.
def envoy_extension_cc_test(
        name,
        extension_names,
        **kwargs):
    _apply_to_extension_allow_for_test(envoy_cc_test, name, extension_names, **kwargs)

def envoy_extension_cc_test_library(
        name,
        extension_names,
        **kwargs):
    _apply_to_extension_allow_for_test(envoy_cc_test_library, name, extension_names, **kwargs)

def envoy_extension_cc_mock(
        name,
        extension_names,
        **kwargs):
    _apply_to_extension_allow_for_test(envoy_cc_mock, name, extension_names, **kwargs)

def envoy_extension_cc_test_binary(
        name,
        extension_names,
        **kwargs):
    _apply_to_extension_allow_for_test(envoy_cc_test_binary, name, extension_names, **kwargs)

def envoy_extension_cc_benchmark_binary(
        name,
        extension_names,
        **kwargs):
    _apply_to_extension_allow_for_test(envoy_cc_benchmark_binary, name, extension_names, **kwargs)

def envoy_extension_benchmark_test(
        name,
        extension_names,
        **kwargs):
    _apply_to_extension_allow_for_test(envoy_benchmark_test, name, extension_names, **kwargs)

# Similar to envoy_cc_test, all extension py_tests should use this version of envoy_py_test
def envoy_extension_py_test(
        name,
        extension_names,
        **kwargs):
    _apply_to_extension_allow_for_test(envoy_py_test, name, extension_names, **kwargs)
