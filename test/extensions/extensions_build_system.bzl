load("//bazel:envoy_build_system.bzl", "envoy_benchmark_test", "envoy_cc_benchmark_binary", "envoy_cc_mock", "envoy_cc_test", "envoy_cc_test_binary", "envoy_cc_test_library")
load("@envoy_build_config//:extensions_build_config.bzl", "EXTENSIONS")

def _apply_to_extension_allow_for_test(func, name, extension_name, **kwargs):
    if not extension_name in EXTENSIONS:
        return

    func(name, **kwargs)

# All extension tests should use this version of envoy_cc_test(). It allows compiling out
# tests for extensions that the user does not wish to include in their build.
# @param extension_name should match an extension listed in EXTENSIONS.
def envoy_extension_cc_test(
        name,
        extension_name,
        **kwargs):
    _apply_to_extension_allow_for_test(envoy_cc_test, name, extension_name, **kwargs)

def envoy_extension_cc_test_library(
        name,
        extension_name,
        **kwargs):
    _apply_to_extension_allow_for_test(envoy_cc_test_library, name, extension_name, **kwargs)

def envoy_extension_cc_mock(
        name,
        extension_name,
        **kwargs):
    _apply_to_extension_allow_for_test(envoy_cc_mock, name, extension_name, **kwargs)

def envoy_extension_cc_test_binary(
        name,
        extension_name,
        **kwargs):
    _apply_to_extension_allow_for_test(envoy_cc_test_binary, name, extension_name, **kwargs)

def envoy_extension_cc_benchmark_binary(
        name,
        extension_name,
        **kwargs):
    _apply_to_extension_allow_for_test(envoy_cc_benchmark_binary, name, extension_name, **kwargs)

def envoy_extension_benchmark_test(
        name,
        extension_name,
        **kwargs):
    _apply_to_extension_allow_for_test(envoy_benchmark_test, name, extension_name, **kwargs)
