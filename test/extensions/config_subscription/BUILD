load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_select_admin_functionality",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "scoped_rds_integration_lib",
    hdrs = ["scoped_rds_integration.h"],
    deps = [
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "scoped_rds_integration",
    size = "large",
    srcs = [
        "scoped_rds_integration.cc",
    ],
    deps = [
        ":scoped_rds_integration_lib",
        "//source/common/config:api_version_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/config_subscription/grpc:grpc_collection_subscription_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config:v2_link_hacks",
        "//test/integration:http_integration_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "leds_integration",
    size = "large",
    srcs = ["leds_integration.cc"],
    tags = [
        "cpu:3",
    ],
    deps = [
        "//source/extensions/config_subscription/grpc:grpc_collection_subscription_lib",
        "//test/config:utility_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:network_utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "ads_integration",
    size = "large",
    srcs = envoy_select_admin_functionality(
        ["ads_integration.cc"],
    ),
    tags = [
        "cpu:3",
    ],
    deps = [
        "//source/common/config:protobuf_link_hacks",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/config_subscription/grpc:grpc_collection_subscription_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration:ads_integration_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)
