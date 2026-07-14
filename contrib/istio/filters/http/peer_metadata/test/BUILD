load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    deps = [
        "//contrib/istio/filters/http/peer_metadata/source:config",
        "//source/common/network:address_lib",
        "//test/common/stream_info:test_util",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//contrib/istio/filters/http/peer_metadata/source:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "peer_metadata_integration_test",
    srcs = ["peer_metadata_integration_test.cc"],
    deps = [
        "//contrib/istio/filters/common/source:metadata_object_lib",
        "//contrib/istio/filters/http/peer_metadata/source:config",
        "//contrib/istio/filters/network/metadata_exchange/source:config",
        "//source/common/common:base64_lib",
        "//source/extensions/filters/http/router:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)
