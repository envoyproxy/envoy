load("@rules_cc//cc:defs.bzl", "cc_test")
load("//:bazel/rules.bzl", "patched_bssl_filegroup")

licenses(["notice"])  # Apache 2

# Create rules for processing BoringSSL test files. This creates one genrule
# per test file, which copies that file from BoringSSL, applying any necessary
# patches. This also creates a filegroup called :patched_bssl_tests which
# contains all the resulting copied & patched test files.
patched_bssl_filegroup(
    name = "patched_bssl_tests",
    srcs = [
        "crypto/bcm_support.h",
        "crypto/bio/bio_test.cc",
        "crypto/digest/digest_test.cc",
        "crypto/err/err_test.cc",
        "crypto/hmac/hmac_test.cc",
        "crypto/internal.h",
        "crypto/pkcs8/pkcs12_test.cc",
        "crypto/rand/rand_test.cc",
        "crypto/rsa/rsa_test.cc",
        "crypto/stack/stack_test.cc",
        "crypto/test/file_test.cc",
        "crypto/test/file_test.h",
        "crypto/test/file_test_gtest.cc",
        "crypto/test/file_util.cc",
        "crypto/test/file_util.h",
        "crypto/test/test_data.cc",
        "crypto/test/test_data.h",
        "crypto/test/test_util.cc",
        "crypto/test/test_util.h",
        "crypto/test/der_trailing_data.h",
        "crypto/test/der_trailing_data.cc",
        "crypto/x509/x509_test.cc",
        "ssl/ssl_c_test.c",
        "ssl/ssl_test.cc",
    ],
)

filegroup(
    name = "handwritten_tests",
    srcs = [
        "test_asn1.cc",
        "test_bn.cc",
        "test_cipher.cc",
        "test_crypto.cc",
        "test_ec_key.cc",
        "test_err.cc",
        "test_evp.cc",
        "test_hmac.cc",
        "test_misc.cc",
        "test_pem.cc",
        "test_rsa.cc",
        "test_sha256.cc",
        "test_ssl.cc",
        "test_stack.cc",
        "test_x509.cc",
        "test_x509v3.cc",
    ] + glob([
        "certs/*.h",
    ]),
)

# Main unit test target for bssl-compat
# This test suite validates the bssl-compat layer by running both:
#   1. Hand-written tests that exercise the BoringSSL API mappings to OpenSSL
#   2. Selected BoringSSL tests to ensure compatibility
cc_test(
    name = "utests-bssl-compat",
    srcs = [
        "main.cc",
        ":handwritten_tests",
        ":patched_bssl_tests",
    ],
    data = [
        "@boringssl//:test_data",
    ],
    env = {
        "BORINGSSL_TEST_DATA_ROOT": "./external/boringssl",
    },
    includes = [
        ".",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@bssl-compat//:crypto",
        "@bssl-compat//:ssl",
        "@com_google_googletest//:gtest",
    ],
)

# The same unit tests as above, but linked against BoringSSL proper, rather than
# bssl-compat. This test acts as a sanity check, ensuring that the bssl-compat
# utests also pass when running them against the real BoringSSL.
cc_test(
    name = "utests-boringssl",
    srcs = [
        "main.cc",
        ":handwritten_tests",
        ":patched_bssl_tests",
    ],
    data = [
        "@boringssl//:test_data",
    ],
    env = {
        "BORINGSSL_TEST_DATA_ROOT": "./external/boringssl",
    },
    includes = [
        ".",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@boringssl//:crypto",
        "@boringssl//:ssl",
        "@com_google_googletest//:gtest",
    ],
)
