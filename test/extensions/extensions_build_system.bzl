load("//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_cc_mock")
load("@envoy_build_config//:extensions_build_config.bzl", "EXTENSIONS")

# All extension tests should use this version of envoy_cc_test(). It allows compiling out
# tests for extensions that the user does not wish to include in their build.
# @param extension_name should match an extension listed in EXTENSIONS.
def envoy_extension_cc_test(name,
                            extension_name,
                            **kwargs):
    if not extension_name in EXTENSIONS:
        return

    envoy_cc_test(name, **kwargs)

def envoy_extension_cc_mock(name,
                            extension_name,
                            **kwargs):
    if not extension_name in EXTENSIONS:
        return

    envoy_cc_mock(name, **kwargs)