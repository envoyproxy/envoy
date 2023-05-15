load("@envoy_build_config//:extensions_build_config.bzl", "EXTENSIONS")

def _selected_extension_target(target):
    return target + "_envoy_extension"

def fuzz_filters_targets(filters):
    all_extensions = EXTENSIONS

    return [v for k, v in all_extensions.items() if (k in filters)]

