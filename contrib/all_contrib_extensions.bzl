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
    "envoy.compression.qatzstd.compressor",
]
X86_SKIP_CONTRIB_TARGETS = [
    "envoy.tls.key_providers.kae",
]
PPC_SKIP_CONTRIB_TARGETS = [
    "envoy.tls.key_providers.cryptomb",
    "envoy.tls.key_providers.qat",
    "envoy.tls.key_providers.kae",
    "envoy.matching.input_matchers.hyperscan",
    "envoy.network.connection_balance.dlb",
    "envoy.regex_engines.hyperscan",
    "envoy.compression.qatzip.compressor",
    "envoy.compression.qatzstd.compressor",
]

# BoringSSL-FIPS historically only skipped qatzip and kae on x86_64
BORINGSSL_FIPS_SKIP_CONTRIB_TARGETS = [
    "envoy.compression.qatzip.compressor",
    "envoy.tls.key_providers.kae",
]

# AWS-LC needs to skip additional Intel-specific crypto providers
AWS_LC_SKIP_CONTRIB_TARGETS = [
    "envoy.tls.key_providers.cryptomb",
    "envoy.tls.key_providers.qat",
    "envoy.tls.key_providers.kae",
    "envoy.compression.qatzip.compressor",
    "envoy.compression.qatzstd.compressor",
]

def envoy_all_contrib_extensions(denylist = []):
    return [v + "_envoy_extension" for k, v in CONTRIB_EXTENSIONS.items() if not k in denylist]

SELECTED_CONTRIB_EXTENSIONS = select({
    "//bazel:linux_aarch64": envoy_all_contrib_extensions(ARM64_SKIP_CONTRIB_TARGETS),
    "//bazel:linux_ppc": envoy_all_contrib_extensions(PPC_SKIP_CONTRIB_TARGETS),
    "//bazel:using_aws_lc": envoy_all_contrib_extensions(AWS_LC_SKIP_CONTRIB_TARGETS),
    "//bazel:using_boringssl_fips": envoy_all_contrib_extensions(BORINGSSL_FIPS_SKIP_CONTRIB_TARGETS),
    "//conditions:default": envoy_all_contrib_extensions(X86_SKIP_CONTRIB_TARGETS),
})
