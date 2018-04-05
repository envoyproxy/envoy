load("//bazel:envoy_build_system.bzl", "envoy_cc_test")
load("@envoy_build_config//:extensions_build_config.bzl", "EXTENSIONS")

# FIXFIX comment
def envoy_extension_cc_test(name,
                            extension_name,
                            **kwargs):
    if not extension_name in EXTENSIONS:
        return

    envoy_cc_test(name, **kwargs)
