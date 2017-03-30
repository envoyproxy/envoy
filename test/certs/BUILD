package(default_visibility = ["//visibility:public"])

genrule(
    name = "test_certs",
    outs = [
        "unittestcert.csr",
        "unittestcert.pem",
        "unittestcert_expired.csr",
        "unittestcert_expired.pem",
        "unittestkey.pem",
        "unittestkey_expired.pem",
    ],
    cmd = "$(location gen_test_certs.sh) $(@D)",
    tools = ["gen_test_certs.sh"],
)
