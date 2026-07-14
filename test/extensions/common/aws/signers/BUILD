load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "sigv4_signer_impl_test",
    srcs = ["sigv4_signer_impl_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:message_lib",
        "//source/extensions/common/aws:credentials_provider_base_lib",
        "//source/extensions/common/aws/signers:sigv4_signer_impl_lib",
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "sigv4_signer_corpus_test",
    srcs = ["sigv4_signer_corpus_test.cc"],
    data = ["@aws-c-auth-testdata//:sigv4_tests"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:message_lib",
        "//source/extensions/common/aws:credentials_provider_base_lib",
        "//source/extensions/common/aws/signers:sigv4_signer_impl_lib",
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "sigv4a_signer_corpus_test",
    srcs = ["sigv4a_signer_corpus_test.cc"],
    data = ["@aws-c-auth-testdata//:sigv4a_tests"],
    rbe_pool = "4core",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:message_lib",
        "//source/extensions/common/aws:credentials_provider_base_lib",
        "//source/extensions/common/aws/signers:sigv4a_signer_impl_lib",
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "sigv4a_signer_impl_test",
    srcs = ["sigv4a_signer_impl_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:message_lib",
        "//source/extensions/common/aws:credentials_provider_base_lib",
        "//source/extensions/common/aws/signers:sigv4a_signer_impl_lib",
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)
