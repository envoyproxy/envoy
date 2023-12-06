load(":contrib_build_config.bzl", "CONTRIB_EXTENSIONS")

# linter requires indirection for @bazel_tools definitions
def envoy_contrib_linux_x86_64_constraints():
    return [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ]

def envoy_contrib_linux_aarch64_constraints():
    return [
        "@platforms//os:linux",
        "@platforms//cpu:aarch64",
    ]

ARM64_SKIP_CONTRIB_TARGETS = [
    "envoy.tls.key_providers.cryptomb",
    "envoy.tls.key_providers.qat",
    "envoy.network.connection_balance.dlb",
    "envoy.compression.qatzip.compressor",
]
PPC_SKIP_CONTRIB_TARGETS = [
    "envoy.tls.key_providers.cryptomb",
    "envoy.tls.key_providers.qat",
    "envoy.matching.input_matchers.hyperscan",
    "envoy.network.connection_balance.dlb",
    "envoy.regex_engines.hyperscan",
    "envoy.compression.qatzip.compressor",
]

def envoy_all_contrib_extensions(denylist = []):
    return [v + "_envoy_extension" for k, v in CONTRIB_EXTENSIONS.items() if not k in denylist]
