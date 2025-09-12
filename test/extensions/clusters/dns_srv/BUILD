load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "dns_srv_cluster_integration_test",
    srcs = ["dns_srv_cluster_integration_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/clusters/dns_srv:cluster",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/extensions/clusters/dns_srv/v3:pkg_cc_proto",
    ],
)
