load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "metadata_object_test",
    srcs = ["metadata_object_test.cc"],
    deps = [
        "//contrib/istio/filters/common/source:metadata_object_lib",
        "//envoy/registry",
    ],
)

envoy_cc_test(
    name = "hashable_string_test",
    srcs = ["hashable_string_test.cc"],
    deps = [
        "//contrib/istio/filters/common/source:hashable_string_lib",
        "//envoy/common:hashable_interface",
        "//source/extensions/filters/common/set_filter_state:filter_config_lib",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "istio_two_proxy_integration_lib",
    hdrs = ["istio_two_proxy_integration_base.h"],
    deps = [
        "//contrib/istio/filters/common/source:metadata_object_lib",
        "//source/common/common:base64_lib",
        "//test/config:utility_lib",
        "//test/integration:http_integration_lib",
        "//test/integration:integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/peer_metadata/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "istio_two_proxy_integration_test",
    srcs = ["istio_two_proxy_integration_test.cc"],
    deps = [
        ":istio_two_proxy_integration_lib",
        "//contrib/istio/filters/http/istio_stats/source:istio_stats",
        "//contrib/istio/filters/http/peer_metadata/source:config",
        "//source/extensions/filters/http/router:config",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "istio_hbone_two_proxy_integration_test",
    srcs = ["istio_hbone_two_proxy_integration_test.cc"],
    deps = [
        "//contrib/istio/filters/common/source:metadata_object_lib",
        "//contrib/istio/filters/common/source:workload_discovery_lib",
        "//contrib/istio/filters/http/istio_stats/source:istio_stats",
        "//contrib/istio/filters/http/peer_metadata/source:config",
        "//contrib/istio/filters/network/peer_metadata/source:config",
        "//source/common/common:base64_lib",
        "//source/extensions/bootstrap/internal_listener:config",
        "//source/extensions/filters/http/router:config",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/transport_sockets/internal_upstream:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/config:utility_lib",
        "//test/integration:integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/bootstrap/internal_listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "istio_tcp_two_proxy_integration_test",
    srcs = ["istio_tcp_two_proxy_integration_test.cc"],
    data = ["//test/config/integration/certs"],
    deps = [
        "//contrib/istio/filters/common/source:metadata_object_lib",
        "//contrib/istio/filters/common/source:workload_discovery_lib",
        "//contrib/istio/filters/http/istio_stats/source:istio_stats",
        "//contrib/istio/filters/network/metadata_exchange/source:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/transport_sockets/tls:config",
        "//test/config:utility_lib",
        "//test/integration:integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/metadata_exchange/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)
