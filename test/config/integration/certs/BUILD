load("@envoy_toolshed//certs:certs.bzl", "generated_certs")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# Fixtures produced by @envoy_toolshed//certs:gen from certs.spec. Keep this list in
# sync with the spec.
CERTS = [
    "cacert.pem",
    "cacert_info.h",
    "client2_chain.pem",
    "client2cert.pem",
    "client2cert_hash.h",
    "client_ecdsacert.pem",
    "client_ecdsacert_hash.h",
    "clientcert.pem",
    "clientcert_hash.h",
    "expired_cert.pem",
    "expired_cert_hash.h",
    "expired_cert_info.h",
    "intermediate_ca_2cert.pem",
    "intermediate_ca_2cert_info.h",
    "intermediate_ca_cert_chain.pem",
    "intermediate_cacert.pem",
    "intermediate_cacert_info.h",
    "intermediate_partial_ca_cert_chain.pem",
    "long_server_ocsp_resp.der",
    "long_servercert.pem",
    "long_servercert_hash.h",
    "long_servercert_info.h",
    "server2cert.pem",
    "server2cert_hash.h",
    "server2cert_info.h",
    "server_ecdsa_ocsp_resp.der",
    "server_ecdsa_p384_ocsp_resp.der",
    "server_ecdsa_p384cert.pem",
    "server_ecdsa_p384cert_hash.h",
    "server_ecdsa_p521_ocsp_resp.der",
    "server_ecdsa_p521cert.pem",
    "server_ecdsa_p521cert_hash.h",
    "server_ecdsacert.pem",
    "server_ecdsacert_hash.h",
    "server_ocsp_resp.der",
    "servercert.pem",
    "servercert_hash.h",
    "servercert_info.h",
    "upstreamcacert.pem",
    "upstreamcacert_info.h",
    "upstreamcert.pem",
    "upstreamcert_hash.h",
    "upstreamlocalhostcert.pem",
    "upstreamlocalhostcert_hash.h",
]

# Includes all generated certificates needed for integration tests.
generated_certs(
    name = "certs",
    srcs = glob([
        "*.cfg",
        "*key.pem",
    ]),
    outs = CERTS,
    spec = "certs.spec",
    # Checked-in fixtures that are not generated: the post-quantum CA, the
    # Google root bundle, the NUL-in-SAN certificate, private keys and configs.
    static_srcs = glob([
        "*.cfg",
        "*key.pem",
        "google_root_certs.pem",
        "pqc_cacert.pem",
        "pqc_cacert_info.h",
        "san_nul_servercert.pem",
    ]),
    visibility = ["//visibility:public"],
)

envoy_cc_test_library(
    name = "hashes",
    hdrs = [
        "client2cert_hash.h",
        "client_ecdsacert_hash.h",
        "clientcert_hash.h",
        "expired_cert_hash.h",
        "long_servercert_hash.h",
        "server2cert_hash.h",
        "server_ecdsa_p384cert_hash.h",
        "server_ecdsa_p521cert_hash.h",
        "server_ecdsacert_hash.h",
        "servercert_hash.h",
        "upstreamcert_hash.h",
        "upstreamlocalhostcert_hash.h",
    ] + glob(["*hash.h"]),
)

envoy_cc_test_library(
    name = "certs_info",
    hdrs = [
        "cacert_info.h",
        "expired_cert_info.h",
        "intermediate_ca_2cert_info.h",
        "intermediate_cacert_info.h",
        "long_servercert_info.h",
        "server2cert_info.h",
        "servercert_info.h",
        "upstreamcacert_info.h",
    ] + glob(["*info.h"]),
)
