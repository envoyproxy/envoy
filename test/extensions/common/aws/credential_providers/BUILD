load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_credentials_provider_test",
    srcs = ["config_credentials_provider_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/aws/credential_providers:config_credentials_provider_lib",
    ],
)

envoy_cc_test(
    name = "instance_profile_credentials_provider_test",
    srcs = ["instance_profile_credentials_provider_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/aws/credential_providers:instance_profile_credentials_provider_lib",
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "webidentity_credentials_provider_test",
    srcs = ["webidentity_credentials_provider_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/aws:credentials_provider_base_lib",
        "//source/extensions/common/aws:metadata_fetcher_lib",
        "//source/extensions/common/aws/credential_providers:webidentity_credentials_provider_lib",
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "assume_role_credentials_provider_test",
    srcs = ["assume_role_credentials_provider_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/aws:credentials_provider_base_lib",
        "//source/extensions/common/aws:metadata_fetcher_lib",
        "//source/extensions/common/aws/credential_providers:assume_role_credentials_provider_lib",
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "container_credentials_provider_test",
    srcs = ["container_credentials_provider_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/aws:credentials_provider_base_lib",
        "//source/extensions/common/aws:metadata_fetcher_lib",
        "//source/extensions/common/aws/credential_providers:container_credentials_provider_lib",
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "environment_credentials_provider_test",
    srcs = ["environment_credentials_provider_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/aws/credential_providers:environment_credentials_provider_lib",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test(
    name = "inline_credentials_provider_test",
    srcs = ["inline_credentials_provider_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/aws/credential_providers:inline_credentials_provider_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "credentials_file_credentials_provider_test",
    srcs = ["credentials_file_credentials_provider_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/aws:utility_lib",
        "//source/extensions/common/aws/credential_providers:credentials_file_credentials_provider_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "iam_roles_anywhere_credentials_provider_test",
    srcs = ["iam_roles_anywhere_credentials_provider_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/aws:utility_lib",
        "//source/extensions/common/aws/credential_providers:credentials_file_credentials_provider_lib",
        "//source/extensions/common/aws/credential_providers:iam_roles_anywhere_credentials_provider_lib",
        "//source/extensions/common/aws/credential_providers:iam_roles_anywhere_x509_credentials_provider_lib",
        "//source/extensions/common/aws/signers:iam_roles_anywhere_sigv4_signer_impl_lib",
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "iam_roles_anywhere_x509_credentials_provider_test",
    srcs = ["iam_roles_anywhere_x509_credentials_provider_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/aws:utility_lib",
        "//source/extensions/common/aws/credential_providers:credentials_file_credentials_provider_lib",
        "//source/extensions/common/aws/credential_providers:iam_roles_anywhere_credentials_provider_lib",
        "//source/extensions/common/aws/credential_providers:iam_roles_anywhere_x509_credentials_provider_lib",
        "//source/extensions/common/aws/signers:iam_roles_anywhere_sigv4_signer_impl_lib",
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
