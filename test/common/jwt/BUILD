load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "test_common_lib",
    hdrs = ["test_common.h"],
    deps = [
        "//source/common/jwt:jwt_lib",
    ],
)

envoy_cc_test(
    name = "check_audience_test",
    srcs = ["check_audience_test.cc"],
    deps = [
        "//source/common/jwt:jwt_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "jwt_test",
    srcs = ["jwt_test.cc"],
    deps = [
        "//source/common/jwt:jwt_lib",
        "//source/common/protobuf",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "jwks_test",
    srcs = ["jwks_test.cc"],
    deps = [
        ":test_common_lib",
        "//source/common/jwt:jwt_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "simple_lru_cache_test",
    srcs = ["simple_lru_cache_test.cc"],
    deps = [
        "//source/common/jwt:simple_lru_cache_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "verify_x509_test",
    srcs = ["verify_x509_test.cc"],
    deps = [
        ":test_common_lib",
        "//source/common/jwt:jwt_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "verify_audiences_test",
    srcs = ["verify_audiences_test.cc"],
    deps = [
        ":test_common_lib",
        "//source/common/jwt:jwt_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "jwt_time_test",
    srcs = ["jwt_time_test.cc"],
    deps = [
        ":test_common_lib",
        "//source/common/jwt:jwt_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "verify_jwk_rsa_test",
    srcs = ["verify_jwk_rsa_test.cc"],
    deps = [
        ":test_common_lib",
        "//source/common/jwt:jwt_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "verify_jwk_rsa_pss_test",
    srcs = ["verify_jwk_rsa_pss_test.cc"],
    deps = [
        ":test_common_lib",
        "//source/common/jwt:jwt_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "verify_jwk_ec_test",
    srcs = ["verify_jwk_ec_test.cc"],
    deps = [
        ":test_common_lib",
        "//source/common/jwt:jwt_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "verify_jwk_hmac_test",
    srcs = ["verify_jwk_hmac_test.cc"],
    deps = [
        ":test_common_lib",
        "//source/common/jwt:jwt_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "verify_jwk_okp_test",
    srcs = ["verify_jwk_okp_test.cc"],
    deps = [
        ":test_common_lib",
        "//source/common/jwt:jwt_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "verify_pem_rsa_test",
    srcs = ["verify_pem_rsa_test.cc"],
    deps = [
        ":test_common_lib",
        "//source/common/jwt:jwt_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "verify_pem_ec_test",
    srcs = ["verify_pem_ec_test.cc"],
    deps = [
        ":test_common_lib",
        "//source/common/jwt:jwt_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "verify_pem_okp_test",
    srcs = ["verify_pem_okp_test.cc"],
    deps = [
        ":test_common_lib",
        "//source/common/jwt:jwt_lib",
        "//test/test_common:utility_lib",
    ],
)
