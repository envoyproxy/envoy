load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_test_binary",
    "envoy_package",
    "envoy_select_google_grpc",
)

licenses(["notice"])  # Apache 2

envoy_package()

# This is a envoy_cc_test_binary instead of an envoy_cc_test because we don't want it to be run
# when `bazel test //test/...` is called.
envoy_cc_test_binary(
    name = "gcp_traffic_director_integration_test",
    # The test relies on the Google gRPC library.
    srcs = envoy_select_google_grpc(
        ["gcp_traffic_director_integration_test.cc"],
        "@envoy",
    ),
    data = [
        "@envoy//test/config/integration/certs",
    ],
    external_deps = [
        "abseil_strings",
    ],
    repository = "@envoy",
    deps = [
        "//library/common:envoy_main_interface_lib_no_stamp",
        "//library/common/data:utility_lib",
        "//library/common/types:c_types_lib",
        "//test/common/integration:base_client_integration_test_lib",
        "@envoy//source/common/config:api_version_lib",
        "@envoy//source/common/grpc:google_grpc_creds_lib",
        "@envoy//source/common/protobuf:utility_lib_header",
        "@envoy//source/extensions/clusters/strict_dns:strict_dns_cluster_lib",
        "@envoy//source/extensions/config_subscription/grpc:grpc_mux_lib",
        "@envoy//source/extensions/config_subscription/grpc:grpc_subscription_lib",
        "@envoy//source/extensions/config_subscription/grpc:new_grpc_mux_lib",
        "@envoy//source/extensions/health_checkers/http:health_checker_lib",
        "@envoy//test/common/grpc:grpc_client_integration_lib",
        "@envoy//test/integration:http_integration_lib",
        "@envoy//test/test_common:environment_lib",
        "@envoy//test/test_common:network_utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_build_config//:test_extensions",
    ],
)
