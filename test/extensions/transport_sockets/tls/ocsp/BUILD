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
        "//test/extensions/transport_sockets/tls/ocsp/test_data:certs",
    ],
    external_deps = ["ssl"],
    deps = [
        "//source/common/filesystem:filesystem_lib",
        "//source/extensions/transport_sockets/tls:utility_lib",
        "//source/extensions/transport_sockets/tls/ocsp:ocsp_lib",
        "//test/extensions/transport_sockets/tls:ssl_socket_test",
        "//test/extensions/transport_sockets/tls:ssl_test_utils",
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
    deps = [
        "//source/extensions/transport_sockets/tls/ocsp:asn1_utility_lib",
        "//test/extensions/transport_sockets/tls:ssl_test_utils",
    ],
)
