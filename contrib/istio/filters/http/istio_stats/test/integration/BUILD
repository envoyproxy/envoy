load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# Integration suite for the Istio `istio.stats` HTTP filter, migrating the
# istio/proxy `stats_plugin` two-sidecar e2e onto the two-Envoy fixture: every
# case runs a real client sidecar and a real server sidecar that perform the
# genuine metadata-exchange handshake over the wire, with assertions on each
# sidecar's stat store (core HTTP stats, the istio.stats CEL engine, the
# filter-state dimension contract, waypoint/baggage, gRPC, and ECDS delivery).
envoy_cc_test(
    name = "istio_stats_integration_test",
    srcs = ["istio_stats_integration_test.cc"],
    deps = [
        "//contrib/istio/filters/common/test:istio_two_proxy_integration_lib",
        "//contrib/istio/filters/http/istio_stats/source:istio_stats",
        "//contrib/istio/filters/http/peer_metadata/source:config",
        "//source/common/buffer:buffer_lib",
        "//source/common/grpc:common_lib",
        "//source/extensions/filters/http/grpc_stats:config",
        "//source/extensions/filters/http/rbac:config",
        "//source/extensions/filters/http/router:config",
        "//source/extensions/filters/http/set_filter_state:config",
        "//test/test_common:utility_lib",
    ],
)
