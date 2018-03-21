licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "jwt_test",
    srcs = [
        "jwt_test.cc",
    ],
    deps = [
        "//source/common/jwt_authn:jwt_lib",
        "//source/exe:envoy_common_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "jwks_test",
    srcs = [
        "jwks_test.cc",
    ],
    deps = [
        "//source/common/jwt_authn:jwt_lib",
        "//source/exe:envoy_common_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "verify_pem_test",
    srcs = [
        "verify_pem_test.cc",
    ],
    deps = [
        "//source/common/jwt_authn:jwt_lib",
        "//source/exe:envoy_common_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "verify_jwk_rsa_test",
    srcs = [
        "verify_jwk_rsa_test.cc",
    ],
    deps = [
        "//source/common/jwt_authn:jwt_lib",
        "//source/exe:envoy_common_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "verify_jwk_ec_test",
    srcs = [
        "verify_jwk_ec_test.cc",
    ],
    deps = [
        "//source/common/jwt_authn:jwt_lib",
        "//source/exe:envoy_common_lib",
        "//test/test_common:utility_lib",
    ],
)
