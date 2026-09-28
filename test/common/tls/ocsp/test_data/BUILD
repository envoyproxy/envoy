load("@envoy_toolshed//certs:certs.bzl", "generated_certs")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# Fixtures produced by @envoy_toolshed//certs:gen from certs.spec.
CERTS = [
    "ca_cert.pem",
    "ecdsa_cert.pem",
    "ecdsa_ocsp_resp.der",
    "good_cert.pem",
    "good_ocsp_resp.der",
    "good_ocsp_resp_info.h",
    "intermediate_ca_cert.pem",
    "multiple_cert_ocsp_resp.der",
    "responder_key_hash_ocsp_resp.der",
    "revoked_cert.pem",
    "revoked_ocsp_resp.der",
    "unknown_ocsp_resp.der",
]

generated_certs(
    name = "certs",
    srcs = glob([
        "*.cfg",
        "*_key.pem",
    ]),
    outs = CERTS,
    spec = "certs.spec",
    static_srcs = glob([
        "*.cfg",
        "*_key.pem",
    ]),
)

# Timestamps of good_ocsp_resp.der, which used to be scraped out of the
# human-readable dump that `openssl ocsp -resp_text` produced.
envoy_cc_test_library(
    name = "ocsp_resp_infos",
    hdrs = [
        "good_ocsp_resp_info.h",
    ],
)
