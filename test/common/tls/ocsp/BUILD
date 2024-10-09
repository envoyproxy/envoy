load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "ocsp_test",
    srcs = [
        "ocsp_test.cc",
    ],
    data = [
        "//test/common/tls/ocsp/test_data:certs",
    ],
    external_deps = ["ssl"],
    rbe_pool = "2core",
    deps = [
        "//source/common/filesystem:filesystem_lib",
        "//source/common/tls:utility_lib",
        "//source/common/tls/ocsp:ocsp_lib",
        "//test/common/tls:ssl_socket_test",
        "//test/common/tls:ssl_test_utils",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_cc_test(
    name = "asn1_utility_test",
    srcs = [
        "asn1_utility_test.cc",
    ],
    external_deps = ["ssl"],
    rbe_pool = "2core",
    deps = [
        "//source/common/tls/ocsp:asn1_utility_lib",
        "//test/common/tls:ssl_test_utils",
    ],
)
