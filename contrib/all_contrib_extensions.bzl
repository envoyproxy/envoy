load(":contrib_build_config.bzl", "CONTRIB_EXTENSIONS")

def envoy_all_contrib_extensions():
    return [v + "_envoy_extension" for v in CONTRIB_EXTENSIONS.values()]
