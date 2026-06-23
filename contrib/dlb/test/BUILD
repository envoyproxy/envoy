load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# Disabled along with the envoy.network.connection_balance.dlb extension —
# see https://github.com/envoyproxy/envoy/issues/45491 and the matching
# patch in //contrib:contrib_build_config.bzl. The "manual" tag excludes
# this test from wildcard builds and tests under //contrib/..., avoiding
# the broken @dlb fetch. Drop it when the upstream mirror is healthy.
envoy_cc_test(
    name = "config_test",
    srcs = select({
        "//bazel:linux_x86_64": ["config_test.cc"],
        "//conditions:default": [],
    }),
    rbe_pool = "6gig",
    tags = ["manual"],
    deps = [
        "//contrib/dlb/source:connection_balancer",
        "//source/common/protobuf:utility_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:status_utility_lib",
        "@envoy_api//contrib/envoy/extensions/network/connection_balance/dlb/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
