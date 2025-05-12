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
