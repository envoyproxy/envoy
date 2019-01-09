licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "signer_impl_test",
    srcs = ["signer_impl_test.cc"],
    deps = [
        "//source/common/aws:signer_impl_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:message_lib",
        "//test/mocks/aws:aws_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "credentials_provider_impl_test",
    srcs = ["credentials_provider_impl_test.cc"],
    deps = [
        "//source/common/aws:credentials_provider_impl_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/aws:aws_mocks",
        "//test/mocks/event:event_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_cc_test(
    name = "metadata_fetcher_impl_test",
    srcs = ["metadata_fetcher_impl_test.cc"],
    deps = [
        "//source/common/aws:metadata_fetcher_impl_lib",
        "//source/common/http:header_map_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:connection_mocks",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_cc_test(
    name = "region_provider_impl_test",
    srcs = ["region_provider_impl_test.cc"],
    deps = [
        "//source/common/aws:region_provider_impl_lib",
        "//test/test_common:environment_lib",
    ],
)
