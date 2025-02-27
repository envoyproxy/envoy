load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "iam_roles_anywhere_x509_credentials_provider_impl_test",
    srcs = ["iam_roles_anywhere_x509_credentials_provider_impl_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/aws:credentials_provider_impl_lib",
        "//source/extensions/common/aws:metadata_fetcher_lib",
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/api:api_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:listener_factory_context_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:cluster_update_callbacks_handle_mocks",
        "//test/mocks/upstream:cluster_update_callbacks_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:thread_local_cluster_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "iam_roles_anywhere_credentials_provider_impl_test",
    srcs = ["iam_roles_anywhere_credentials_provider_impl_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/aws:credentials_provider_impl_lib",
        "//source/extensions/common/aws:metadata_fetcher_lib",
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/api:api_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:listener_factory_context_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:cluster_update_callbacks_handle_mocks",
        "//test/mocks/upstream:cluster_update_callbacks_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:thread_local_cluster_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto",
    ],
)
