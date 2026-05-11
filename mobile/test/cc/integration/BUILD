load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_cc_test_library", "envoy_mobile_package")
load("//bazel:envoy_mobile_cc_test.bzl", "envoy_cc_test_with_engine_builder")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_test_with_engine_builder(
    name = "send_headers_test",
    srcs = ["send_headers_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/http/filters/assertion:filter_cc_proto",
        "//test/common/integration:engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test_with_engine_builder(
    name = "send_trailers_test",
    srcs = ["send_trailers_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/http/filters/assertion:filter_cc_proto",
        "//test/common/integration:engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test_with_engine_builder(
    name = "send_data_test",
    srcs = ["send_data_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/http/filters/assertion:filter_cc_proto",
        "//test/common/integration:engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test_with_engine_builder(
    name = "receive_headers_test",
    srcs = ["receive_headers_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/integration:engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test_with_engine_builder(
    name = "receive_data_test",
    srcs = ["receive_data_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/integration:engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test_with_engine_builder(
    name = "receive_trailers_test",
    srcs = ["receive_trailers_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/integration:engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test_with_engine_builder(
    name = "lifetimes_test",
    srcs = ["lifetimes_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/integration:engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test_library(
    name = "test_base_lib",
    srcs = [
        "base/test_engine_and_server.cc",
    ],
    hdrs = [
        "base/test_engine_and_server.h",
        "base/test_engine_builder.h",
    ],
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_base_lib",
        "//library/cc:envoy_engine_cc_lib_no_stamp",
        "//test/common/integration:test_server_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/strings",
        "@envoy//source/extensions/clusters/static:static_cluster_lib",
        "@envoy//source/extensions/load_balancing_policies/round_robin:config",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/http_11_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/raw_buffer/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = ["base/integration_test.cc"],
    repository = "@envoy",
    deps = [
        ":test_base_lib",
        "//library/cc:envoy_engine_cc_lib_no_stamp",
        "//test/common/http/filters/assertion:filter_cc_proto",
        "@envoy//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)
