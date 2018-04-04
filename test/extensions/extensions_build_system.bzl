load("//bazel:envoy_build_system.bzl", "envoy_cc_test")
load("@envoy_build_config//:extensions_build_config.bzl", "ENVOY_BUILD_CONFIG")

# FIXFIX comment
def envoy_extension_cc_test(name,
                            extension_name,
                            **kwargs):
    if not ENVOY_BUILD_CONFIG[extension_name]["enabled"]:
        return

    envoy_cc_test(name, **kwargs)
