load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_package",
    "envoy_select_boringssl",
)

licenses(["notice"])  # Apache 2

envoy_package()

# envoy_cc_test(
#     name = "credentials_provider_impl_test",
#     srcs = ["credentials_provider_impl_test.cc"],
#     rbe_pool = "6gig",
#     deps = [
#         "//source/extensions/common/aws:aws_cluster_manager_lib",
#         "//source/extensions/common/aws:credentials_provider_impl_lib",
#         "//source/extensions/common/aws:metadata_fetcher_lib",
#         "//source/extensions/common/aws/signers:sigv4_signer_impl_lib",
#         "//test/extensions/common/aws:aws_mocks",
#         "//test/mocks/api:api_mocks",
#         "//test/mocks/event:event_mocks",
#         "//test/mocks/runtime:runtime_mocks",
#         "//test/mocks/server:factory_context_mocks",
#         "//test/mocks/server:listener_factory_context_mocks",
#         "//test/mocks/upstream:cluster_manager_mocks",
#         "//test/mocks/upstream:cluster_update_callbacks_handle_mocks",
#         "//test/mocks/upstream:cluster_update_callbacks_mocks",
#         "//test/mocks/upstream:host_mocks",
#         "//test/mocks/upstream:thread_local_cluster_mocks",
#         "//test/test_common:environment_lib",
#         "//test/test_common:simulated_time_system_lib",
#         "//test/test_common:test_runtime_lib",
#         "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto",
#     ],
# )


envoy_cc_test(
    name = "config_credentials_provider_test",
    srcs = ["config_credentials_provider_test.cc"],
    rbe_pool = "6gig",
    deps = [

    ],
)

envoy_cc_test(
    name = "instance_profile_credentials_provider_test",
    srcs = ["instance_profile_credentials_provider_test.cc"],
    rbe_pool = "6gig",
    deps = [

    ],
)


envoy_cc_test(
    name = "webidentity_credentials_provider_test",
    srcs = ["webidentity_credentials_provider_test.cc"],
    rbe_pool = "6gig",
    deps = [

    ],
)


envoy_cc_test(
    name = "container_credentials_provider_test",
    srcs = ["container_credentials_provider_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/aws:metadata_fetcher_lib",
        "//source/extensions/common/aws:aws_cluster_manager_lib",
        "//source/extensions/common/aws:credentials_provider_base_lib",
        "//source/extensions/common/aws/signers:sigv4_signer_impl_lib",
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
        "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto",
    ],
)


envoy_cc_test(
    name = "credentials_file_credentials_provider_test",
    srcs = ["credentials_file_credentials_provider_test.cc"],
    rbe_pool = "6gig",
    deps = [

    ],
)