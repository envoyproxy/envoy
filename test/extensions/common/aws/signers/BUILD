load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_package",
    "envoy_select_boringssl",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "iam_roles_anywhere_sigv4_signer_impl_test",
    srcs = ["iam_roles_anywhere_sigv4_signer_impl_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:message_lib",
        "//source/extensions/common/aws:sigv4_signer_impl_lib",
        "//source/extensions/common/aws:sigv4a_signer_impl_lib",
        "//source/extensions/common/aws:aws_cluster_manager_lib",
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)
