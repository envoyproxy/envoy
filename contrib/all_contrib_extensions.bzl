load(":contrib_build_config.bzl", "CONTRIB_EXTENSIONS")

ARM64_SKIP_CONTRIB_TARGETS = ["envoy.tls.key_providers.cryptomb"]
PPC_SKIP_CONTRIB_TARGETS = ["envoy.tls.key_providers.cryptomb"]

def envoy_all_contrib_extensions(denylist = []):
    return [v + "_envoy_extension" for k, v in CONTRIB_EXTENSIONS.items() if not k in denylist]
