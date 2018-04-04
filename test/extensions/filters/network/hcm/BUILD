licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/config:filter_json_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/router:rds_lib",
        "//source/extensions/filters/http/dynamo:config",
        "//source/extensions/filters/network/hcm:config",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)
