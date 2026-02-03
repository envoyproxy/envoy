load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "transform_stat_test",
    srcs = ["transform_stat_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "//source/extensions/matching/actions/transform_stat:config",
        "//source/extensions/matching/actions/transform_stat:transform_stat_lib",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/matching/actions/transform_stat/v3:pkg_cc_proto",
    ],
)
