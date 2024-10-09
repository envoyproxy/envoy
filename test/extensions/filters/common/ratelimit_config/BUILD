load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "ratelimit_config_test_proto",
    srcs = ["ratelimit_config_test.proto"],
    deps = [
        "@envoy_api//envoy/config/route/v3:pkg",
    ],
)

envoy_cc_test(
    name = "ratelimit_config_test",
    srcs = ["ratelimit_config_test.cc"],
    deps = [
        ":ratelimit_config_test_proto_cc_proto",
        "//source/common/http:header_map_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:config_lib",
        "//source/extensions/filters/common/ratelimit_config:ratelimit_config_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/ratelimit:ratelimit_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)
