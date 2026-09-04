licenses(["notice"])  # Apache 2

# This build file exists ONLY to expose BoringSSL source files to Envoy's
# compat/openssl (boringssl-source) layer, which copies and rewrites individual
# headers and sources.
#
# It deliberately defines NO cc_library targets. Do not link against this.
# For the actual crypto/ssl libraries use the `boringssl` repository.
#
# NOTE: this archive MUST stay in lockstep with the `boringssl` version that
# Envoy builds //:crypto and //:ssl from, or the rewritten sources will be
# compiled against mismatched headers. Both repositories are fetched from the
# same `boringssl` entry in repository_locations.bzl.
#
# This mirrors the overlay BUILD of the `boringssl-source` bzlmod module so that
# the compat/openssl layer can reference `@boringssl-source` identically in both
# WORKSPACE and bzlmod builds.
exports_files(glob([
    "include/**/*",
    "crypto/**/*",
    "ssl/**/*",
]))

filegroup(
    name = "test_data",
    srcs = glob([
        "crypto/x509/test/*.pem",
        "crypto/pkcs8/test/*.p12",
    ]),
    visibility = ["//visibility:public"],
)
