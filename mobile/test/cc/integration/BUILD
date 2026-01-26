load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_test(
    name = "send_headers_test",
    srcs = ["send_headers_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/http/filters/assertion:filter_cc_proto",
        "//test/common/integration:engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "client_send_headers_test",
    srcs = ["client_send_headers_test.cc"],
    repository = "@envoy",
    tags = ["requires-net:ipv4"],
    deps = [
        "//library/cc:client_engine_builder_lib",
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/http/filters/assertion:filter_cc_proto",
        "//test/common/integration:client_engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@com_google_absl//absl/synchronization",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "send_trailers_test",
    srcs = ["send_trailers_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/http/filters/assertion:filter_cc_proto",
        "//test/common/integration:engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "client_send_trailers_test",
    srcs = ["client_send_trailers_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:client_engine_builder_lib",
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/http/filters/assertion:filter_cc_proto",
        "//test/common/integration:client_engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@com_google_absl//absl/synchronization",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "send_data_test",
    srcs = ["send_data_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/http/filters/assertion:filter_cc_proto",
        "//test/common/integration:engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "client_send_data_test",
    srcs = ["client_send_data_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:client_engine_builder_lib",
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/http/filters/assertion:filter_cc_proto",
        "//test/common/integration:client_engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@com_google_absl//absl/synchronization",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "receive_headers_test",
    srcs = ["receive_headers_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/integration:engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "client_receive_headers_test",
    srcs = ["client_receive_headers_test.cc"],
    repository = "@envoy",
    deps = [
       "//library/cc:client_engine_builder_lib",
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/integration:client_engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@com_google_absl//absl/synchronization",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "receive_data_test",
    srcs = ["receive_data_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/integration:engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "client_receive_data_test",
    srcs = ["client_receive_data_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:client_engine_builder_lib",
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/integration:client_engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@com_google_absl//absl/synchronization",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "receive_trailers_test",
    srcs = ["receive_trailers_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/integration:engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "client_receive_trailers_test",
    srcs = ["client_receive_trailers_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:client_engine_builder_lib",
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/integration:client_engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@com_google_absl//absl/synchronization",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "lifetimes_test",
    srcs = ["lifetimes_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/integration:engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "client_lifetimes_test",
    srcs = ["client_lifetimes_test.cc"],
    repository = "@envoy",
    tags = ["requires-net:ipv4"],
    deps = [
        "//library/cc:client_engine_builder_lib",
        "//library/common:engine_types_lib",
        "//library/common/http:header_utility_lib",
        "//test/common/integration:client_engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@com_google_absl//absl/synchronization",
        "@envoy_build_config//:test_extensions",
    ],
)
