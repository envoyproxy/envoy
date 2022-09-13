load(":contrib_build_config.bzl", "CONTRIB_EXTENSIONS")

# linter requires indirection for @bazel_tools definitions
def envoy_contrib_linux_x86_64_constraints():
    return [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ]

ARM64_SKIP_CONTRIB_TARGETS = [
    "envoy.tls.key_providers.cryptomb",
    "envoy.tls.key_providers.qat",
    "envoy.matching.input_matchers.hyperscan",
    "envoy.network.connection_balance.dlb",
    "envoy.regex_engines.hyperscan",
]
PPC_SKIP_CONTRIB_TARGETS = [
    "envoy.tls.key_providers.cryptomb",
    "envoy.tls.key_providers.qat",
    "envoy.matching.input_matchers.hyperscan",
    "envoy.network.connection_balance.dlb",
    "envoy.regex_engines.hyperscan",
]

def envoy_all_contrib_extensions(denylist = []):
    return [v + "_envoy_extension" for k, v in CONTRIB_EXTENSIONS.items() if not k in denylist]
